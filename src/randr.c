/* randr.c - RandR multi-monitor support
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wconfig.h"

#ifdef USE_RANDR

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <X11/Xlib.h>

#include "randr.h"
#include "monitor_layout.h"
#include "window.h"
#include "framewin.h"
#include "xinerama.h"
#include "wmspec.h"
#include "dock.h"
#include "workspace.h"
#include "actions.h"
#include "defaults.h"

#define RANDR_MAX_OUTPUTS     32
#define RANDR_DEBOUNCE_DELAY  100   /* milliseconds */
#define RANDR_RESTORE_RETRY_DELAY 500  /* milliseconds */
#define RANDR_RESTORE_ATTEMPTS 5

typedef struct {
    RROutput xid;
    RRCrtc crtc;
    int x, y, w, h;
    Rotation rotation;     /* current CRTC rotation */
    Bool connected;        /* RR_Connected */
    Bool activate_pending; /* connection transition, not an intentional disable */
    Bool stale;            /* mark-scan scratch flag */
    char name[64];
} WRandROutput;

/* Active heads in Xinerama order, with the output name of each one */
typedef struct {
    WMRect rects[RANDR_MAX_OUTPUTS];
    char names[RANDR_MAX_OUTPUTS][64];
    int count;
    int primary;
} WRandRHeads;

/* Position of the dock or clip relative to the monitor it is on */
typedef struct {
    Bool valid;
    char head[64];
    int dx, dy;            /* offset from the head origin */
    int hw, hh;            /* head size when the anchor was taken */
    Bool right;            /* dock side */
} WRandRAnchor;

typedef struct {
    WRandROutput outputs[RANDR_MAX_OUTPUTS];
    int n_outputs;
    WRandRHeads heads;
    WMPropList *placements; /* monitor set signature -> dock/clip anchors */
    Bool restore_pending;  /* connected monitors changed; look for a saved layout */
    int restore_attempts;
} WRandRState;

/* Initial output scan to populate state from scratch */
static void wRandR_Scan(WScreen *scr, WRandRState *state)
{
    XRRScreenResources *sr;
    int i;

    state->n_outputs = 0;

    sr = XRRGetScreenResourcesCurrent(dpy, scr->root_win);
    if (!sr)
        return;

    for (i = 0; i < sr->noutput && state->n_outputs < RANDR_MAX_OUTPUTS; i++) {
        XRROutputInfo *oi;
        WRandROutput *o;

        oi = XRRGetOutputInfo(dpy, sr, sr->outputs[i]);
        if (!oi)
            continue;

        o = &state->outputs[state->n_outputs++];
        memset(o, 0, sizeof(*o));
        o->xid = sr->outputs[i];
        o->connected = (oi->connection == RR_Connected);
        o->activate_pending = o->connected && oi->crtc == None;
        o->crtc = oi->crtc;
        strncpy(o->name, oi->name, sizeof(o->name) - 1);

        if (oi->crtc != None) {
            XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, sr, oi->crtc);
            if (ci) {
                o->x = ci->x;
                o->y = ci->y;
                o->w = ci->width;
                o->h = ci->height;
                o->rotation = ci->rotation;
                XRRFreeCrtcInfo(ci);
            }
        }

        XRRFreeOutputInfo(oi);
    }

    XRRFreeScreenResources(sr);
}

/* Primary head detection */
static int wRandR_PrimaryHeadIndex(Window root, const RROutput *xids, int count)
{
    RROutput primary_xid = XRRGetOutputPrimary(dpy, root);
    int i;

    if (primary_xid != None) {
        for (i = 0; i < count; i++) {
            if (xids[i] == primary_xid)
                return i;
        }
    }
    return 0;
}

/* Push RandR geometry into the Xinerama head layer */
static Bool wRandR_ApplyToXinerama(WScreen *scr, WRandRState *state)
{
    WMRect new_screens[RANDR_MAX_OUTPUTS];
    RROutput new_xids[RANDR_MAX_OUTPUTS];   /* parallel: XID of each head */
    const char *new_names[RANDR_MAX_OUTPUTS];
    int count = 0;
    int old_count;
    int i;
    XWindowAttributes attrs;
    Bool changed;

    if (!XGetWindowAttributes(dpy, scr->root_win, &attrs)) {
        wwarning("RandR: could not query root window geometry");
        return False;
    }
    changed = scr->scr_width != attrs.width || scr->scr_height != attrs.height;
    scr->scr_width = attrs.width;
    scr->scr_height = attrs.height;

    for (i = 0; i < state->n_outputs && count < RANDR_MAX_OUTPUTS; i++) {
        WRandROutput *o = &state->outputs[i];
        int j, dup = 0;

        /* Collect outputs that are truly active */
        if (o->crtc == None || o->w == 0 || o->h == 0)
            continue;

        /* Deduplicate mirrored outputs that share the same CRTC origin */
        for (j = 0; j < count; j++) {
            if (new_screens[j].pos.x == o->x && new_screens[j].pos.y == o->y) {
                dup = 1;
                break;
            }
        }
        if (dup)
            continue;

        new_screens[count].pos.x = o->x;
        new_screens[count].pos.y = o->y;
        new_screens[count].size.width = o->w;
        new_screens[count].size.height = o->h;
        new_xids[count] = o->xid;
        new_names[count] = o->name;
        count++;
    }

    /* Fallback: if every output disappeared, use the full virtual screen so
     * we never end up with zero heads */
    if (count == 0) {
        new_screens[0].pos.x = 0;
        new_screens[0].pos.y = 0;
        new_screens[0].size.width = scr->scr_width;
        new_screens[0].size.height = scr->scr_height;
        new_xids[0] = None;
        new_names[0] = "";
        count = 1;
    }

    old_count = wXineramaHeads(scr);

    if (old_count != count || !scr->xine_info.screens) {
        changed = True;
    } else {
        for (i = 0; i < count; i++) {
            WMRect *old = &scr->xine_info.screens[i];
            WMRect *new = &new_screens[i];

            if (old->pos.x != new->pos.x || old->pos.y != new->pos.y ||
                old->size.width != new->size.width || old->size.height != new->size.height)
                changed = True;
        }
    }

    /* Replace the screens array */
    if (scr->xine_info.screens)
        wfree(scr->xine_info.screens);

    scr->xine_info.screens = wmalloc(sizeof(WMRect) * (count + 1));
    memcpy(scr->xine_info.screens, new_screens, sizeof(WMRect) * count);
    scr->xine_info.count = count;

    scr->xine_info.primary_head = wRandR_PrimaryHeadIndex(scr->root_win, new_xids, count);

    memcpy(state->heads.rects, new_screens, sizeof(WMRect) * count);
    for (i = 0; i < count; i++) {
        strncpy(state->heads.names[i], new_names[i], sizeof(state->heads.names[i]) - 1);
        state->heads.names[i][sizeof(state->heads.names[i]) - 1] = '\0';
    }
    state->heads.count = count;
    state->heads.primary = scr->xine_info.primary_head;

    /* Reallocate per-head usable area arrays if the head count changed */
    if (old_count != count) {
        wfree(scr->usableArea);
        wfree(scr->totalUsableArea);
        scr->usableArea = wmalloc(sizeof(WArea) * count);
        scr->totalUsableArea = wmalloc(sizeof(WArea) * count);
    }

    /* Seed usable areas from new geometry */
    for (i = 0; i < count; i++) {
        WMRect *r = &scr->xine_info.screens[i];

        scr->usableArea[i].x1 = scr->totalUsableArea[i].x1 = r->pos.x;
        scr->usableArea[i].y1 = scr->totalUsableArea[i].y1 = r->pos.y;
        scr->usableArea[i].x2 = scr->totalUsableArea[i].x2 = r->pos.x + r->size.width;
        scr->usableArea[i].y2 = scr->totalUsableArea[i].y2 = r->pos.y + r->size.height;
    }

    return changed;
}

static int wRandR_HeadByName(const WRandRHeads *heads, const char *name)
{
    int i;

    if (!name[0])
        return -1;
    for (i = 0; i < heads->count; i++) {
        if (strcmp(heads->names[i], name) == 0)
            return i;
    }
    return -1;
}

/* Head containing a point, or the closest one */
static int wRandR_NearestHead(const WRandRHeads *heads, int x, int y)
{
    long best_distance = -1;
    int best = heads->primary;
    int i;

    for (i = 0; i < heads->count; i++) {
        const WMRect *r = &heads->rects[i];
        long dx = 0, dy = 0, distance;

        if (x < r->pos.x)
            dx = r->pos.x - x;
        else if (x >= r->pos.x + (int)r->size.width)
            dx = x - (r->pos.x + (int)r->size.width) + 1;
        if (y < r->pos.y)
            dy = r->pos.y - y;
        else if (y >= r->pos.y + (int)r->size.height)
            dy = y - (r->pos.y + (int)r->size.height) + 1;

        distance = dx * dx + dy * dy;
        if (best_distance < 0 || distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

/* Head sharing the largest area with a rectangle, or -1 if none does */
static int wRandR_HeadForRect(const WRandRHeads *heads, int x, int y, int w, int h)
{
    long best_area = 0;
    int best = -1;
    int i;

    for (i = 0; i < heads->count; i++) {
        const WMRect *r = &heads->rects[i];
        int x1 = WMAX(x, r->pos.x);
        int y1 = WMAX(y, r->pos.y);
        int x2 = WMIN(x + w, r->pos.x + (int)r->size.width);
        int y2 = WMIN(y + h, r->pos.y + (int)r->size.height);
        long area;

        if (x2 <= x1 || y2 <= y1)
            continue;
        area = (long)(x2 - x1) * (y2 - y1);
        if (area > best_area) {
            best_area = area;
            best = i;
        }
    }
    return best;
}

/* Sorted output names identify a monitor set */
static void wRandR_HeadsSignature(const WRandRHeads *heads, char *buf, size_t size)
{
    const char *names[RANDR_MAX_OUTPUTS];
    int count = 0;
    int i, j;

    buf[0] = '\0';
    for (i = 0; i < heads->count; i++) {
        const char *name = heads->names[i];

        if (!name[0]) {
            buf[0] = '\0';
            return;
        }
        for (j = count; j > 0 && strcmp(names[j - 1], name) > 0; j--)
            names[j] = names[j - 1];
        names[j] = name;
        count++;
    }
    for (i = 0; i < count; i++) {
        if (i > 0)
            wstrlcat(buf, ",", size);
        wstrlcat(buf, names[i], size);
    }
}

/* Map an offset from the old head size to the new one, keeping the distance
 * to whichever edge of the head it was closer to */
static int wRandR_EdgeRelative(int offset, int old_size, int new_size, int item_size)
{
    if (offset + item_size / 2 <= old_size / 2)
        return offset;
    return new_size - (old_size - offset);
}

/* Move a span [pos + lo, pos + hi) into [min, max). If it does not fit, keep
 * the main item [pos, pos + item) visible and show as much of the rest as
 * possible. */
static int wRandR_ClampSpan(int pos, int lo, int hi, int item, int min, int max)
{
    if (hi - lo <= max - min) {
        if (pos + hi > max)
            pos = max - hi;
        if (pos + lo < min)
            pos = min - lo;
        return pos;
    }
    if (lo >= 0)
        return min;
    if (hi <= item)
        return max - item;
    if (pos + item > max)
        pos = max - item;
    if (pos < min)
        pos = min;
    return pos;
}

static void wRandR_CaptureAnchor(const WRandRHeads *heads, int x, int y, WRandRAnchor *anchor)
{
    int head = wRandR_NearestHead(heads, x + wPreferences.icon_size / 2,
                                  y + wPreferences.icon_size / 2);
    const WMRect *r = &heads->rects[head];

    memset(anchor, 0, sizeof(*anchor));
    if (heads->count == 0)
        return;
    anchor->valid = True;
    strncpy(anchor->head, heads->names[head], sizeof(anchor->head) - 1);
    anchor->dx = x - r->pos.x;
    anchor->dy = y - r->pos.y;
    anchor->hw = r->size.width;
    anchor->hh = r->size.height;
}

/* Find the anchored head (or the fallback one) and the position on it */
static int wRandR_ResolveAnchor(const WRandRHeads *heads, const WRandRAnchor *anchor,
                                int fallback, int *x, int *y)
{
    int head = wRandR_HeadByName(heads, anchor->head);
    const WMRect *r;

    if (head < 0)
        head = fallback;
    r = &heads->rects[head];
    *x = r->pos.x + wRandR_EdgeRelative(anchor->dx, anchor->hw, r->size.width,
                                        wPreferences.icon_size);
    *y = r->pos.y + wRandR_EdgeRelative(anchor->dy, anchor->hh, r->size.height,
                                        wPreferences.icon_size);
    return head;
}

/* Leftmost or rightmost head, for a dock whose monitor went away */
static int wRandR_DockFallbackHead(const WRandRHeads *heads, Bool right)
{
    int best = heads->primary;
    int i;

    if (wPreferences.keep_dock_on_primary_head)
        return best;
    for (i = 0; i < heads->count; i++) {
        const WMRect *r = &heads->rects[i];
        const WMRect *b = &heads->rects[best];

        if (right ? r->pos.x + (int)r->size.width > b->pos.x + (int)b->size.width
                  : r->pos.x < b->pos.x)
            best = i;
    }
    return best;
}

static WMPropList *wRandR_AnchorToPL(const WRandRAnchor *anchor, Bool dock)
{
    WMPropList *dict, *key, *value;
    char buf[32];

    dict = WMCreatePLDictionary(NULL, NULL);

#define PUT(name, val) do { \
        key = WMCreatePLString(name); value = WMCreatePLString(val); \
        WMPutInPLDictionary(dict, key, value); \
        WMReleasePropList(key); WMReleasePropList(value); \
    } while (0)
#define PUTINT(name, val) do { snprintf(buf, sizeof(buf), "%i", (val)); PUT(name, buf); } while (0)
    PUT("Head", anchor->head);
    PUTINT("X", anchor->dx);
    PUTINT("Y", anchor->dy);
    PUTINT("Width", anchor->hw);
    PUTINT("Height", anchor->hh);
    if (dock)
        PUT("Side", anchor->right ? "Right" : "Left");
#undef PUTINT
#undef PUT
    return dict;
}

static Bool wRandR_AnchorFromPL(WMPropList *dict, WRandRAnchor *anchor)
{
    const char *keys[] = { "Head", "X", "Y", "Width", "Height" };
    const char *values[5];
    WMPropList *key, *value;
    int i;

    memset(anchor, 0, sizeof(*anchor));
    if (!dict || !WMIsPLDictionary(dict))
        return False;
    for (i = 0; i < 5; i++) {
        key = WMCreatePLString(keys[i]);
        value = WMGetFromPLDictionary(dict, key);
        WMReleasePropList(key);
        if (!value || !WMIsPLString(value))
            return False;
        values[i] = WMGetFromPLString(value);
    }
    anchor->dx = atoi(values[1]);
    anchor->dy = atoi(values[2]);
    anchor->hw = atoi(values[3]);
    anchor->hh = atoi(values[4]);
    if (!values[0][0] || anchor->hw <= 0 || anchor->hh <= 0)
        return False;
    strncpy(anchor->head, values[0], sizeof(anchor->head) - 1);

    key = WMCreatePLString("Side");
    value = WMGetFromPLDictionary(dict, key);
    WMReleasePropList(key);
    anchor->right = value && WMIsPLString(value) &&
        strcasecmp(WMGetFromPLString(value), "Right") == 0;
    anchor->valid = True;
    return True;
}

/* Remember where the dock and clip are for the current monitor set */
static void wRandR_RememberPlacement(WRandRState *state, const WRandRHeads *heads,
                                     const WRandRAnchor *dock, const WRandRAnchor *clip)
{
    char signature[RANDR_MAX_OUTPUTS * 64];
    WMPropList *entry, *key, *value;

    wRandR_HeadsSignature(heads, signature, sizeof(signature));
    if (!signature[0] || (!dock->valid && !clip->valid))
        return;

    if (!state->placements)
        state->placements = WMCreatePLDictionary(NULL, NULL);

    entry = WMCreatePLDictionary(NULL, NULL);
    if (dock->valid) {
        key = WMCreatePLString("Dock");
        value = wRandR_AnchorToPL(dock, True);
        WMPutInPLDictionary(entry, key, value);
        WMReleasePropList(key);
        WMReleasePropList(value);
    }
    if (clip->valid) {
        key = WMCreatePLString("Clip");
        value = wRandR_AnchorToPL(clip, False);
        WMPutInPLDictionary(entry, key, value);
        WMReleasePropList(key);
        WMReleasePropList(value);
    }
    key = WMCreatePLString(signature);
    WMPutInPLDictionary(state->placements, key, entry);
    WMReleasePropList(key);
    WMReleasePropList(entry);
}

/* Use the placement remembered for this monitor set, if there is one */
static void wRandR_RecallPlacement(WRandRState *state, const WRandRHeads *heads,
                                   WRandRAnchor *dock, WRandRAnchor *clip)
{
    char signature[RANDR_MAX_OUTPUTS * 64];
    WMPropList *entry, *key;
    WRandRAnchor anchor;

    wRandR_HeadsSignature(heads, signature, sizeof(signature));
    if (!signature[0] || !state->placements)
        return;

    key = WMCreatePLString(signature);
    entry = WMGetFromPLDictionary(state->placements, key);
    WMReleasePropList(key);
    if (!entry || !WMIsPLDictionary(entry))
        return;

    key = WMCreatePLString("Dock");
    if (wRandR_AnchorFromPL(WMGetFromPLDictionary(entry, key), &anchor))
        *dock = anchor;
    WMReleasePropList(key);
    key = WMCreatePLString("Clip");
    if (wRandR_AnchorFromPL(WMGetFromPLDictionary(entry, key), &anchor))
        *clip = anchor;
    WMReleasePropList(key);
}

static void wRandR_CapturePlacement(WScreen *scr, const WRandRHeads *heads,
                                    WRandRAnchor *dock, WRandRAnchor *clip)
{
    WDock *current_clip = NULL;

    memset(dock, 0, sizeof(*dock));
    memset(clip, 0, sizeof(*clip));

    if (scr->dock) {
        wRandR_CaptureAnchor(heads, scr->dock->x_pos, scr->dock->y_pos, dock);
        dock->right = scr->dock->on_right_side;
    }
    if (!wPreferences.flags.noclip && scr->workspaces && scr->workspace_count > 0)
        current_clip = scr->workspaces[scr->current_workspace]->clip;
    if (current_clip)
        wRandR_CaptureAnchor(heads, current_clip->x_pos, current_clip->y_pos, clip);
}

/* Put the dock and clip back on the monitors they were on, keeping the dock
 * at a head edge and the clip together with its attached icons visible */
static void wRandR_PlaceDockAndClip(WScreen *scr, const WRandRHeads *heads,
                                    const WRandRAnchor *dock_anchor,
                                    const WRandRAnchor *clip_anchor)
{
    int icon = wPreferences.icon_size;
    int head, x, y, k;
    WMRect extent;
    const WMRect *r;

    if (heads->count == 0)
        return;

    if (scr->dock && dock_anchor->valid) {
        head = wRandR_ResolveAnchor(heads, dock_anchor,
                                    wRandR_DockFallbackHead(heads, dock_anchor->right), &x, &y);
        if (wPreferences.keep_dock_on_primary_head && head != heads->primary) {
            head = heads->primary;
            y = heads->rects[head].pos.y +
                wRandR_EdgeRelative(dock_anchor->dy, dock_anchor->hh,
                                    heads->rects[head].size.height, icon);
        }
        r = &heads->rects[head];
        extent = wDockGetExtent(scr->dock);
        y = wRandR_ClampSpan(y, extent.pos.y, extent.pos.y + (int)extent.size.height, icon,
                             r->pos.y, r->pos.y + (int)r->size.height);
        wDockMoveToEdge(scr->dock, head, dock_anchor->right, y);
    }

    if (!wPreferences.flags.noclip && clip_anchor->valid &&
        scr->workspaces && scr->workspace_count > 0) {
        WDock *current = scr->workspaces[scr->current_workspace]->clip;

        if (!current)
            return;
        head = wRandR_ResolveAnchor(heads, clip_anchor, heads->primary, &x, &y);
        r = &heads->rects[head];
        extent = wDockGetExtent(current);
        x = wRandR_ClampSpan(x, extent.pos.x, extent.pos.x + (int)extent.size.width, icon,
                             r->pos.x, r->pos.x + (int)r->size.width);
        y = wRandR_ClampSpan(y, extent.pos.y, extent.pos.y + (int)extent.size.height, icon,
                             r->pos.y, r->pos.y + (int)r->size.height);
        for (k = 0; k < scr->workspace_count; k++) {
            if (scr->workspaces[k]->clip)
                wDockMove(scr->workspaces[k]->clip, x, y);
        }
    }
}

typedef struct {
    int x, y, w, h;
} WRandRBox;

static Bool wRandR_BoxesOverlap(const WRandRBox *a, const WRandRBox *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w &&
        a->y < b->y + b->h && b->y < a->y + a->h;
}

static Bool wRandR_BoxIsFree(const WRandRBox *box, const WRandRBox *obstacles, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (wRandR_BoxesOverlap(box, &obstacles[i]))
            return False;
    }
    return True;
}

/* Areas taken by the dock and the current clip with their icons */
static int wRandR_DockAndClipBoxes(WScreen *scr, WRandRBox *boxes)
{
    int icon = wPreferences.icon_size;
    int count = 0;
    WMRect extent;

    if (scr->dock) {
        extent = wDockGetExtent(scr->dock);
        boxes[count].x = scr->dock->x_pos + extent.pos.x;
        boxes[count].y = scr->dock->y_pos + extent.pos.y;
        boxes[count].w = extent.size.width;
        boxes[count].h = extent.size.height;
        count++;
    }
    if (!wPreferences.flags.noclip && scr->workspaces && scr->workspace_count > 0) {
        WDock *clip = scr->workspaces[scr->current_workspace]->clip;

        if (clip) {
            if (clip->collapsed) {
                extent.pos.x = extent.pos.y = 0;
                extent.size.width = extent.size.height = icon;
            } else {
                extent = wDockGetExtent(clip);
            }
            boxes[count].x = clip->x_pos + extent.pos.x;
            boxes[count].y = clip->y_pos + extent.pos.y;
            boxes[count].w = extent.size.width;
            boxes[count].h = extent.size.height;
            count++;
        }
    }
    return count;
}

static int wRandR_ClampToRange(int pos, int size, int min, int max)
{
    if (pos > max - size)
        pos = max - size;
    if (pos < min)
        pos = min;
    return pos;
}

/* Move a box inside a head the least possible so that it does not cover any
 * obstacle. If there is no such position and the box can be resized, shrink
 * it into the largest part of the head left free by the obstacles. */
static void wRandR_AvoidBoxes(const WMRect *head, const WRandRBox *obstacles, int count,
                              WRandRBox *box, Bool resizable)
{
    int xs[1 + 2 * 2], ys[1 + 2 * 2];
    int hx1 = head->pos.x, hy1 = head->pos.y;
    int hx2 = hx1 + (int)head->size.width, hy2 = hy1 + (int)head->size.height;
    long best_distance = -1;
    WRandRBox best = *box, free_area;
    int nxs = 0, nys = 0, i, j;

    if (wRandR_BoxIsFree(box, obstacles, count))
        return;

    xs[nxs++] = box->x;
    ys[nys++] = box->y;
    for (i = 0; i < count && i < 2; i++) {
        xs[nxs++] = obstacles[i].x - box->w;
        xs[nxs++] = obstacles[i].x + obstacles[i].w;
        ys[nys++] = obstacles[i].y - box->h;
        ys[nys++] = obstacles[i].y + obstacles[i].h;
    }

    for (i = 0; i < nxs; i++) {
        for (j = 0; j < nys; j++) {
            WRandRBox c = *box;
            long distance;

            c.x = wRandR_ClampToRange(xs[i], c.w, hx1, hx2);
            c.y = wRandR_ClampToRange(ys[j], c.h, hy1, hy2);
            if (c.x < hx1 || c.y < hy1 || !wRandR_BoxIsFree(&c, obstacles, count))
                continue;
            distance = labs((long)c.x - box->x) + labs((long)c.y - box->y);
            if (best_distance < 0 || distance < best_distance) {
                best_distance = distance;
                best = c;
            }
        }
    }
    if (best_distance >= 0) {
        *box = best;
        return;
    }
    if (!resizable)
        return;

    /* Cut each obstacle off the side of the head that keeps the most room */
    free_area.x = hx1;
    free_area.y = hy1;
    free_area.w = hx2 - hx1;
    free_area.h = hy2 - hy1;
    for (i = 0; i < count; i++) {
        const WRandRBox *o = &obstacles[i];
        WRandRBox cuts[4] = { free_area, free_area, free_area, free_area };
        long area, best_area = -1;
        int k;

        if (!wRandR_BoxesOverlap(&free_area, o))
            continue;
        cuts[0].w = o->x - free_area.x;
        cuts[1].x = o->x + o->w;
        cuts[1].w = free_area.x + free_area.w - cuts[1].x;
        cuts[2].h = o->y - free_area.y;
        cuts[3].y = o->y + o->h;
        cuts[3].h = free_area.y + free_area.h - cuts[3].y;
        for (k = 0; k < 4; k++) {
            area = cuts[k].w > 0 && cuts[k].h > 0 ? (long)cuts[k].w * cuts[k].h : 0;
            if (area > best_area) {
                best_area = area;
                best = cuts[k];
            }
        }
        if (best_area <= 0)
            return;
        free_area = best;
    }

    box->w = WMIN(box->w, free_area.w);
    box->h = WMIN(box->h, free_area.h);
    box->x = wRandR_ClampToRange(box->x, box->w, free_area.x, free_area.x + free_area.w);
    box->y = wRandR_ClampToRange(box->y, box->h, free_area.y, free_area.y + free_area.h);
}

/* Move a rectangle along with the head it was on. Returns the new head, or -1
 * if the rectangle was on no head. It is kept fully visible if it was before
 * or if its head went away; *changed tells whether its head changed at all. */
static int wRandR_FollowRect(const WRandRHeads *old, const WRandRHeads *cur,
                             int *x, int *y, int w, int h, Bool *changed)
{
    int oh, nh, target;
    const WMRect *orr, *nr;
    Bool was_inside;

    *changed = False;
    oh = wRandR_HeadForRect(old, *x, *y, w, h);
    if (oh < 0)
        return -1;
    nh = wRandR_HeadByName(cur, old->names[oh]);
    target = nh >= 0 ? nh : cur->primary;
    orr = &old->rects[oh];
    nr = &cur->rects[target];

    if (nh >= 0 && orr->pos.x == nr->pos.x && orr->pos.y == nr->pos.y &&
        orr->size.width == nr->size.width && orr->size.height == nr->size.height)
        return target;
    *changed = True;

    was_inside = *x >= orr->pos.x && *y >= orr->pos.y &&
        *x + w <= orr->pos.x + (int)orr->size.width &&
        *y + h <= orr->pos.y + (int)orr->size.height;

    *x = nr->pos.x + wRandR_EdgeRelative(*x - orr->pos.x, orr->size.width, nr->size.width, w);
    *y = nr->pos.y + wRandR_EdgeRelative(*y - orr->pos.y, orr->size.height, nr->size.height, h);
    if (was_inside || nh < 0) {
        *x = wRandR_ClampSpan(*x, 0, w, w, nr->pos.x, nr->pos.x + (int)nr->size.width);
        *y = wRandR_ClampSpan(*y, 0, h, h, nr->pos.y, nr->pos.y + (int)nr->size.height);
    }
    return target;
}

/* Geometry restored when leaving maximized or fullscreen state */
static void wRandR_FollowSavedGeometry(const WRandRHeads *old, const WRandRHeads *cur,
                                       int *x, int *y, unsigned int w, unsigned int h)
{
    Bool changed;

    if (w > 0 && h > 0)
        wRandR_FollowRect(old, cur, x, y, w, h, &changed);
}

/* Keep windows on the monitor they were on, following it when it moves, and
 * bring windows from a removed monitor fully onto the primary one. Windows
 * that were clear of the dock and clip stay clear of them. */
static void wRandR_RelocateWindows(WScreen *scr, const WRandRHeads *old, const WRandRHeads *cur,
                                   const WRandRBox *old_boxes, int old_count,
                                   const WRandRBox *new_boxes, int new_count)
{
    WWindow *wwin;

    if (old->count == 0 || cur->count == 0)
        return;

    for (wwin = scr->focused_window; wwin != NULL; wwin = wwin->prev) {
        int bw, wx, wy, ww, wh, nx, ny, target;
        const WMRect *nr;
        Bool changed;

        if (!wwin->frame || !wwin->frame->core)
            continue;

        bw = HAS_BORDER(wwin) ? scr->frame_border_width : 0;
        wx = wwin->frame_x - bw;
        wy = wwin->frame_y - bw;
        ww = (int)wwin->frame->core->width + 2 * bw;
        wh = (int)wwin->frame->core->height + 2 * bw;

        nx = wx;
        ny = wy;
        target = wRandR_FollowRect(old, cur, &nx, &ny, ww, wh, &changed);
        if (target < 0)
            continue;
        if (!changed) {
            /* Still on the same monitor, but the dock or clip may have come
             * over it from a removed one */
            WRandRBox box = { wx, wy, ww, wh };

            if (wwin->flags.fullscreen || wwin->flags.maximized ||
                wRandR_BoxIsFree(&box, new_boxes, new_count))
                continue;
        }
        nr = &cur->rects[target];

        wRandR_FollowSavedGeometry(old, cur, &wwin->old_geometry.x, &wwin->old_geometry.y,
                                   wwin->old_geometry.width, wwin->old_geometry.height);
        wRandR_FollowSavedGeometry(old, cur, &wwin->bfs_geometry.x, &wwin->bfs_geometry.y,
                                   wwin->bfs_geometry.width, wwin->bfs_geometry.height);

        if (wwin->flags.fullscreen) {
            wWindowConfigure(wwin, nr->pos.x, nr->pos.y, nr->size.width, nr->size.height);
        } else if (wwin->flags.maximized && !wwin->flags.shaded) {
            wWindowMove(wwin, nx + bw, ny + bw);
            wMaximizeWindow(wwin, wwin->flags.maximized | MAX_KEYBOARD, target);
        } else {
            Bool resizable = IS_RESIZABLE(wwin) && !wwin->flags.shaded;
            WRandRBox box = { wx, wy, ww, wh };
            WRandRBox target_box;

            /* Shrink windows larger than their new monitor to fit on it */
            target_box.x = nx;
            target_box.y = ny;
            target_box.w = resizable ? WMIN(ww, (int)nr->size.width) : ww;
            target_box.h = resizable ? WMIN(wh, (int)nr->size.height) : wh;

            if (wRandR_BoxIsFree(&box, old_boxes, old_count) &&
                target_box.x >= nr->pos.x && target_box.y >= nr->pos.y &&
                target_box.x + target_box.w <= nr->pos.x + (int)nr->size.width &&
                target_box.y + target_box.h <= nr->pos.y + (int)nr->size.height)
                wRandR_AvoidBoxes(nr, new_boxes, new_count, &target_box, resizable);

            if (target_box.w != ww || target_box.h != wh) {
                unsigned int cw = target_box.w - 2 * bw;
                unsigned int ch = target_box.h - 2 * bw -
                    wwin->frame->top_width - wwin->frame->bottom_width;

                wWindowConstrainSize(wwin, &cw, &ch);
                wWindowConfigure(wwin, target_box.x + bw, target_box.y + bw, cw, ch);
            } else if (target_box.x != wx || target_box.y != wy) {
                wWindowMove(wwin, target_box.x + bw, target_box.y + bw);
                wWindowSynthConfigureNotify(wwin);
            }
        }
    }
}

/* Bring any off-screen windows in */
static void wRandR_BringAllWindowsInside(WScreen *scr)
{
    WWindow *wwin;
    int i;

    for (wwin = scr->focused_window; wwin != NULL; wwin = wwin->prev) {
        int bw, wx, wy, ww, wh;
        int fully_inside = 0;

        if (!wwin->flags.mapped || wwin->flags.hidden)
            continue;

        bw = HAS_BORDER(wwin) ? scr->frame_border_width : 0;
        wx = wwin->frame_x - bw;
        wy = wwin->frame_y - bw;
        ww = (int)wwin->frame->core->width  + 2 * bw;
        wh = (int)wwin->frame->core->height + 2 * bw;

        /* Skip windows already fully contained within a surviving head */
        for (i = 0; i < wXineramaHeads(scr); i++) {
            WMRect r = wGetRectForHead(scr, i);
            if (wx >= r.pos.x && wy >= r.pos.y &&
                wx + ww <= r.pos.x + (int)r.size.width &&
                wy + wh <= r.pos.y + (int)r.size.height) {
                fully_inside = 1;
                break;
            }
        }

        if (!fully_inside)
            wWindowSnapToHead(wwin);
    }
}

/* Move the dock, clip and windows after the heads changed from old_heads */
static void wRandR_FollowHeads(WScreen *scr, WRandRState *state,
                               const WRandRHeads *old_heads, Bool geometry_changed)
{
    WRandRAnchor dock_anchor, clip_anchor;
    char old_set[RANDR_MAX_OUTPUTS * 64], new_set[RANDR_MAX_OUTPUTS * 64];
    WRandRBox old_boxes[2], new_boxes[2];
    int old_count, new_count;

    old_count = wRandR_DockAndClipBoxes(scr, old_boxes);

    wRandR_HeadsSignature(old_heads, old_set, sizeof(old_set));
    wRandR_HeadsSignature(&state->heads, new_set, sizeof(new_set));

    if (geometry_changed || old_heads->primary != state->heads.primary ||
        strcmp(old_set, new_set) != 0) {
        /* Remember where they were on the previous monitor set, then use
         * the placement remembered for the new one, if any */
        wRandR_CapturePlacement(scr, old_heads, &dock_anchor, &clip_anchor);
        wRandR_RememberPlacement(state, old_heads, &dock_anchor, &clip_anchor);
        wRandR_RecallPlacement(state, &state->heads, &dock_anchor, &clip_anchor);
        wRandR_PlaceDockAndClip(scr, &state->heads, &dock_anchor, &clip_anchor);
    }

    /* Refresh usable areas and EWMH hints */
    wScreenUpdateUsableArea(scr);
    wNETWMUpdateWorkarea(scr);

    if (geometry_changed) {
        new_count = wRandR_DockAndClipBoxes(scr, new_boxes);
        wRandR_RelocateWindows(scr, old_heads, &state->heads,
                               old_boxes, old_count, new_boxes, new_count);
    }

    /* Bring any windows that ended up in dead space to an active head */
    wRandR_BringAllWindowsInside(scr);

    /* Rearrange miniaturized and appicons into the icon yard */
    wArrangeIcons(scr, True);
}


/* Auto-deactivate a physically disconnected output that still holds a CRTC
 *
 * When a cable is pulled or monitor turned off, the X server sets
 * connection=RR_Disconnected but does NOT free the CRTC automatically.
 * We must call XRRSetCrtcConfig with mode=None to release it,
 * which causes the server to fire a fresh batch of RandR events.
 *
 * Returns True if at least one CRTC was freed */
static Bool wRandR_AutoDeactivate(WScreen *scr, WRandRState *state, XRRScreenResources *sr)
{
    Bool deactivated = False;
    int i;

    for (i = 0; i < state->n_outputs; i++) {
        WRandROutput *o = &state->outputs[i];
        Status ret;

        if (o->connected || o->crtc == None || o->stale)
            continue;

        /* Release the CRTC: no mode, no outputs */
        ret = XRRSetCrtcConfig(dpy, sr, o->crtc, sr->timestamp,
                               0, 0, None, RR_Rotate_0, NULL, 0);
        if (ret == RRSetConfigSuccess) {
            wwarning("RandR: released CRTC for disconnected output %s", o->name);
            o->crtc = None;
            o->w = o->h = 0;
            deactivated = True;
        } else {
            wwarning("RandR: failed to release CRTC for output %s", o->name);
        }
    }

    /* Compact and shrink the virtual framebuffer to the bounding box of remaining active outputs */
    if (deactivated) {
        int min_x = scr->scr_width;
        int max_x = 0, max_y = 0;
        int j;

        for (i = 0; i < state->n_outputs; i++) {
            WRandROutput *a = &state->outputs[i];

            if (a->crtc == None || a->w == 0 || a->h == 0)
                continue;

            if (a->x < min_x) min_x = a->x;
            if (a->x + a->w > max_x) max_x = a->x + a->w;
            if (a->y + a->h > max_y) max_y = a->y + a->h;
        }

        /* Slide surviving outputs left to eliminate dead space at the origin */
        if (min_x > 0 && max_x > 0) {
            for (i = 0; i < state->n_outputs; i++) {
                WRandROutput *a = &state->outputs[i];
                XRRCrtcInfo *ci;

                if (a->crtc == None || a->w == 0 || a->h == 0)
                    continue;

                ci = XRRGetCrtcInfo(dpy, sr, a->crtc);
                if (!ci)
                    continue;

                XRRSetCrtcConfig(dpy, sr, a->crtc, sr->timestamp,
                                 a->x - min_x, a->y,
                                 ci->mode, ci->rotation,
                                 ci->outputs, ci->noutput);

                for (j = 0; j < state->n_outputs; j++) {
                    if (state->outputs[j].crtc == a->crtc)
                        state->outputs[j].x -= min_x;
                }

                XRRFreeCrtcInfo(ci);
            }
            max_x -= min_x;
        }

        /* Shrink the virtual framebuffer down to the compacted bounding box */
        if (max_x > 0 && max_y > 0 &&
            (max_x < scr->scr_width || max_y < scr->scr_height)) {
            int mm_w = (int)((long)WidthMMOfScreen(ScreenOfDisplay(dpy, scr->screen))
                              * max_x / scr->scr_width);
            int mm_h = (int)((long)HeightMMOfScreen(ScreenOfDisplay(dpy, scr->screen))
                              * max_y / scr->scr_height);
            XRRSetScreenSize(dpy, scr->root_win, max_x, max_y, mm_w, mm_h);
        }
    }

    return deactivated;
}

/* Auto-activate a newly connected output that has no CRTC assigned yet
 *
 * Mirrors "xrandr --auto": find a free CRTC, pick the preferred mode,
 * place the new head to the right of all active outputs,
 * expand the virtual framebuffer if needed, then call XRRSetCrtcConfig.
 *
 * Returns True if at least one output was activated */
static Bool wRandR_AutoActivate(WScreen *scr, WRandRState *state, XRRScreenResources *sr)
{
    int i, j;

    for (i = 0; i < state->n_outputs; i++) {
        WRandROutput *o = &state->outputs[i];
        XRROutputInfo *oi;
        RRCrtc free_crtc = None;
        Rotation rotation = RR_Rotate_0;
        RRMode best_mode = None;
        int mode_w = 0, mode_h = 0;
        int new_x = 0;
        int new_vw, new_vh;
        int min_w, min_h, max_w, max_h;
        XWindowAttributes attrs;
        Status ret;

        /* Only process newly connected outputs that have no CRTC yet */
        if (!o->connected || !o->activate_pending || o->crtc != None || o->stale)
            continue;

        oi = XRRGetOutputInfo(dpy, sr, o->xid);
        if (!oi)
            continue;

        if (oi->ncrtc == 0 || oi->nmode == 0) {
            XRRFreeOutputInfo(oi);
            continue;
        }

        /* Find a free CRTC that is idle and capable of driving this output */
        for (j = 0; j < oi->ncrtc; j++) {
            XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, sr, oi->crtcs[j]);
            if (ci) {
                if (ci->noutput == 0) {
                    int k;

                    for (k = 0; k < (int)ci->npossible; k++) {
                        if (ci->possible[k] == o->xid) {
                            free_crtc = oi->crtcs[j];
                            if (ci->rotations & RR_Rotate_0)
                                rotation = RR_Rotate_0;
                            else if (ci->rotations & RR_Rotate_90)
                                rotation = RR_Rotate_90;
                            else if (ci->rotations & RR_Rotate_180)
                                rotation = RR_Rotate_180;
                            else
                                rotation = RR_Rotate_270;
                            break;
                        }
                    }
                }
                XRRFreeCrtcInfo(ci);
            }
            if (free_crtc != None)
                break;
        }

        if (free_crtc == None) {
            wwarning("RandR: no free CRTC for output %s", o->name);
            XRRFreeOutputInfo(oi);
            continue;
        }

        /* Preferred mode is first in the list (per RandR spec) */
        best_mode = oi->modes[0];
        XRRFreeOutputInfo(oi);

        /* Look up its pixel dimensions in sr->modes[] */
        for (j = 0; j < sr->nmode; j++) {
            if (sr->modes[j].id == best_mode) {
                mode_w = (int)sr->modes[j].width;
                mode_h = (int)sr->modes[j].height;
                break;
            }
        }

        if (mode_w == 0 || mode_h == 0)
            continue;

        if (rotation & (RR_Rotate_90 | RR_Rotate_270)) {
            int tmp = mode_w;

            mode_w = mode_h;
            mode_h = tmp;
        }

        /* Position it to the right of all currently active outputs, same as GNOME */
        for (j = 0; j < state->n_outputs; j++) {
            WRandROutput *a = &state->outputs[j];

            if (a->crtc != None && a->w > 0) {
                int right = a->x + a->w;

                if (right > new_x)
                    new_x = right;
            }
        }

        /* Expand virtual framebuffer if the new head exceeds current size */
        if (!XGetWindowAttributes(dpy, scr->root_win, &attrs) ||
            !XRRGetScreenSizeRange(dpy, scr->root_win, &min_w, &min_h, &max_w, &max_h)) {
            wwarning("RandR: could not query screen size limits for output %s", o->name);
            continue;
        }
        new_vw = new_x + mode_w;
        if (new_vw < attrs.width)
            new_vw = attrs.width;
        new_vh = mode_h > attrs.height ? mode_h : attrs.height;

        if (new_vw < min_w) new_vw = min_w;
        if (new_vh < min_h) new_vh = min_h;
        if (new_vw > max_w || new_vh > max_h) {
            wwarning("RandR: output %s would exceed the maximum screen size %dx%d",
                     o->name, max_w, max_h);
            continue;
        }

        if (new_vw > attrs.width || new_vh > attrs.height) {
            int mm_w = (int)((long)WidthMMOfScreen(ScreenOfDisplay(dpy, scr->screen))
                          * new_vw / attrs.width);
            int mm_h = (int)((long)HeightMMOfScreen(ScreenOfDisplay(dpy, scr->screen))
                          * new_vh / attrs.height);
            XRRSetScreenSize(dpy, scr->root_win, new_vw, new_vh, mm_w, mm_h);
        }

        ret = XRRSetCrtcConfig(dpy, sr, free_crtc, CurrentTime,
                               new_x, 0, best_mode, rotation,
                               &o->xid, 1);
        if (ret == RRSetConfigSuccess) {
            o->activate_pending = False;
            wwarning("RandR: auto-activated output %s at +%d+0", o->name, new_x);
            /* Re-query resources before assigning another output: CRTC use,
             * framebuffer size and configuration timestamps have changed. */
            return True;
        } else {
            wwarning("RandR: failed to auto-activate output %s", o->name);
        }
    }

    return False;
}

static void wRandRDebounceTimerFired(void *data);

static void wRandR_MarkOutputsAsRestored(WRandRState *state)
{
    int i;

    /* Outputs disabled by the saved layout are not pending hotplug activations. */
    for (i = 0; i < state->n_outputs; i++)
        state->outputs[i].activate_pending = False;
}

/* Apply the saved layout for exactly the connected set of monitors, if any */
static MonitorLayoutStatus wRandR_RestoreSavedLayout(WScreen *scr)
{
    char *path;
    char error[512];
    MonitorLayout *layout;
    MonitorLayoutStatus status;

    /* The monitor editor saves layouts for the default X screen. */
    if (scr->screen != DefaultScreen(dpy))
        return MONITOR_LAYOUT_NONE;

    layout = MonitorLayoutQuery(dpy, scr->root_win, error, sizeof(error));
    if (!layout) {
        wwarning("RandR: could not restore saved monitor layout: %s", error);
        return MONITOR_LAYOUT_FAILED;
    }

    path = wdefaultspathfordomain("WMMonitorLayout");
    status = MonitorLayoutLoadExact(layout, path, error, sizeof(error));
    if (status == MONITOR_LAYOUT_LOADED &&
        !MonitorLayoutApply(dpy, scr->root_win, layout, error, sizeof(error)))
        status = MONITOR_LAYOUT_FAILED;

    if (status == MONITOR_LAYOUT_LOADED)
        wmessage("RandR: restored saved monitor layout from %s", path);
    else if (status == MONITOR_LAYOUT_FAILED)
        wwarning("RandR: saved monitor layout was not applied: %s", error);

    MonitorLayoutFree(layout);
    wfree(path);
    return status;
}

/* Synchronize RandR state with the X server */
static void wRandR_Update(WScreen *scr)
{
    WRandRState *state = (WRandRState *)scr->randr_state;
    XRRScreenResources *sr;
    int i, j;
    Bool geometry_changed, connection_changed = False;
    WRandRHeads old_heads;

    /* Assume all outputs removed until server confirms */
    for (i = 0; i < state->n_outputs; i++)
        state->outputs[i].stale = True;

    /* Re-query server, update or add outputs */
    sr = XRRGetScreenResourcesCurrent(dpy, scr->root_win);
    if (!sr) {
        wwarning("wRandR_Update: XRRGetScreenResourcesCurrent failed");
        return;
    }

    for (i = 0; i < sr->noutput; i++) {
        XRROutputInfo *oi;
        WRandROutput *found = NULL;

        oi = XRRGetOutputInfo(dpy, sr, sr->outputs[i]);
        if (!oi)
            continue;

        /* Match by XID (stable across reconfigures) */
        for (j = 0; j < state->n_outputs; j++) {
            if (state->outputs[j].xid == sr->outputs[i]) {
                found = &state->outputs[j];
                break;
            }
        }

        /* Append new output not seen before */
        if (!found && state->n_outputs < RANDR_MAX_OUTPUTS) {
            found = &state->outputs[state->n_outputs++];
            memset(found, 0, sizeof(*found));
            found->xid = sr->outputs[i];
            strncpy(found->name, oi->name, sizeof(found->name) - 1);
        }

        if (found) {
            if ((oi->connection == RR_Connected) != found->connected)
                connection_changed = True;
            found->stale = False;
            if (oi->connection == RR_Connected && !found->connected)
                found->activate_pending = True;
            if (oi->connection != RR_Connected || oi->crtc != None)
                found->activate_pending = False;
            found->connected = (oi->connection == RR_Connected);
            found->crtc = oi->crtc;
            found->w = 0;
            found->h = 0;
            found->rotation = RR_Rotate_0;

            if (oi->crtc != None) {
                XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, sr, oi->crtc);
                if (ci) {
                    found->x = ci->x;
                    found->y = ci->y;
                    found->w = ci->width;
                    found->h = ci->height;
                    found->rotation = ci->rotation;
                    XRRFreeCrtcInfo(ci);
                }
            }
        }

        XRRFreeOutputInfo(oi);
    }

    /* Remove outputs not reported by server at all; removing a connected
     * one (e.g. a DP-MST dock output) is a connection change. */
    for (i = 0, j = 0; i < state->n_outputs; i++) {
        if (state->outputs[i].stale) {
            if (state->outputs[i].connected)
                connection_changed = True;
            continue;
        }
        if (j != i)
            state->outputs[j] = state->outputs[i];
        j++;
    }
    state->n_outputs = j;

    /* When HotplugMonitor is enabled, actively manage CRTC lifecycle */
    if (wPreferences.hotplug_monitor) {
        Bool changed;
        int delay = RANDR_DEBOUNCE_DELAY;

        if (connection_changed) {
            state->restore_pending = True;
            state->restore_attempts = 0;
        }

        changed = wRandR_AutoDeactivate(scr, state, sr);

        /* A saved layout for the new set of monitors takes precedence over
         * placing new monitors automatically. Docks may expose monitors
         * before their modes are ready, so retry a failed restore briefly. */
        if (!changed && state->restore_pending) {
            switch (wRandR_RestoreSavedLayout(scr)) {
            case MONITOR_LAYOUT_LOADED:
                state->restore_pending = False;
                wRandR_MarkOutputsAsRestored(state);
                changed = True;
                break;
            case MONITOR_LAYOUT_FAILED:
                if (++state->restore_attempts < RANDR_RESTORE_ATTEMPTS) {
                    changed = True;
                    delay = RANDR_RESTORE_RETRY_DELAY;
                } else {
                    state->restore_pending = False;
                }
                break;
            default:
                state->restore_pending = False;
                break;
            }
        }

        if (!changed)
            changed = wRandR_AutoActivate(scr, state, sr);
        if (changed) {
            XRRFreeScreenResources(sr);
            if (scr->randr_debounce_timer)
                WMDeleteTimerHandler(scr->randr_debounce_timer);
            scr->randr_debounce_timer =
                WMAddTimerHandler(delay, wRandRDebounceTimerFired, scr);
            return;
        }
    } else {
        state->restore_pending = False;
    }

    XRRFreeScreenResources(sr);

    /* Apply new geometry to the Xinerama head layer */
    old_heads = state->heads;
    geometry_changed = wRandR_ApplyToXinerama(scr, state);

    if (geometry_changed)
        wDefaultRefreshWorkspaceBackground(scr);

    wRandR_FollowHeads(scr, state, &old_heads, geometry_changed);
}

static void wRandRDebounceTimerFired(void *data)
{
    WScreen *scr = (WScreen *)data;

    scr->randr_debounce_timer = NULL;
    wRandR_Update(scr);
}

/* end of local stuff */

void wRandRInit(WScreen *scr)
{
    WRandRState *state;
    Bool restored;

    if (!w_global.xext.randr.supported)
        return;

    state = wmalloc(sizeof(WRandRState));
    memset(state, 0, sizeof(*state));
    scr->randr_state = state;

    restored = wRandR_RestoreSavedLayout(scr) == MONITOR_LAYOUT_LOADED;
    wRandR_Scan(scr, state);
    if (restored)
        wRandR_MarkOutputsAsRestored(state);
    wRandR_ApplyToXinerama(scr, state);
}

void wRandRTeardown(WScreen *scr)
{
    if (!scr->randr_state)
        return;

    if (scr->randr_debounce_timer) {
        WMDeleteTimerHandler(scr->randr_debounce_timer);
        scr->randr_debounce_timer = NULL;
    }

    if (((WRandRState *)scr->randr_state)->placements)
        WMReleasePropList(((WRandRState *)scr->randr_state)->placements);
    wfree(scr->randr_state);
    scr->randr_state = NULL;
}

/* Move the dock and clip to the placement remembered for the current
 * monitor set, if any. WMState holds the positions of the set that was
 * active on exit, which may not be the current one. */
void wRandRApplySavedPlacement(WScreen *scr)
{
    WRandRState *state = (WRandRState *)scr->randr_state;
    WRandRAnchor dock, clip;

    if (!state)
        return;
    memset(&dock, 0, sizeof(dock));
    memset(&clip, 0, sizeof(clip));
    wRandR_RecallPlacement(state, &state->heads, &dock, &clip);
    wRandR_PlaceDockAndClip(scr, &state->heads, &dock, &clip);
}

/* Dock and clip placements per monitor set, including the current one */
WMPropList *wRandRGetPlacements(WScreen *scr)
{
    WRandRState *state = (WRandRState *)scr->randr_state;
    WRandRAnchor dock, clip;

    if (!state)
        return NULL;
    wRandR_CapturePlacement(scr, &state->heads, &dock, &clip);
    wRandR_RememberPlacement(state, &state->heads, &dock, &clip);
    return state->placements;
}

void wRandRSetPlacements(WScreen *scr, WMPropList *placements)
{
    WRandRState *state = (WRandRState *)scr->randr_state;

    if (!state || !placements || !WMIsPLDictionary(placements))
        return;
    if (state->placements)
        WMReleasePropList(state->placements);
    state->placements = WMDeepCopyPropList(placements);
}

void wRandRHandleNotify(WScreen *scr, XEvent *event)
{
    if (!scr || !scr->randr_state)
        return;

    if (event->type == (w_global.xext.randr.event_base + RRScreenChangeNotify))
        XRRUpdateConfiguration(event);

    /* Debounce: cancel any pending timer, then restart it */
    if (scr->randr_debounce_timer) {
        WMDeleteTimerHandler(scr->randr_debounce_timer);
        scr->randr_debounce_timer = NULL;
    }
    scr->randr_debounce_timer =
        WMAddTimerHandler(RANDR_DEBOUNCE_DELAY, wRandRDebounceTimerFired, scr);
}

#endif /* USE_RANDR */
