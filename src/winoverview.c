/* winoverview.c - full screen overview of the windows in the current workspace
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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "actions.h"
#include "stacking.h"
#include "switchpanel.h"
#include "application.h"
#include "appicon.h"
#include "icon.h"
#include "keybind.h"
#include "xinerama.h"
#include "misc.h"
#include "snapshot.h"
#include "winpreview.h"
#include "winoverview.h"


#define MAX_ENTRIES	64
#define OUTER_MARGIN	48	/* distance between the head edge and the grid */
#define CELL_GAP	32	/* distance between cells */
#define TITLE_GAP	6	/* distance between a title and its thumbnail */
#define FRAME_WIDTH	3	/* width of the selection frame */
#define FRAME_GAP	3	/* distance between the selection frame and the thumbnail */
#define ICON_SIZE	64	/* size of the icon drawn when there is no snapshot */
#define DARKEN		90	/* wallpaper brightness, out of 256 */
#define FONT_SIZE	13
#define MAX_CACHED_HEADS	8
#define PREPARE_DELAY	1500	/* ms to wait for the wallpaper to settle before caching it */
#define HOVER_THRESHOLD	8	/* pointer motion that selects while cycling with the keyboard */

typedef struct {
	WWindow *wwin;		/* only valid while the overview is being built */
	Window client_win;
	RImage *image;		/* snapshot or icon, NULL if none */
	Bool is_icon;
	Bool fresh;		/* image was just grabbed and can refresh the cache */
	Bool live;		/* draw the current content, scaled by the server */
	Bool away;		/* minimized, hidden or on another workspace */
	char *wm_class;
	char *label;
	int nat_w, nat_h;	/* size the thumbnail would have when not scaled */
	int x, y, w, h;		/* thumbnail rectangle */
} OverviewEntry;

static struct {
	WScreen *scr;
	Window win;
	WMRect rect;
	WMFont *font;
	int title_height;

	OverviewEntry entries[MAX_ENTRIES];
	int count;
	int selected;
	int pressed;
	Bool done;
	Window chosen;

	/* keyboard window cycling, see wWindowOverviewCycle() */
	Bool cycling;
	Bool class_only;
	unsigned int hold_mask;	/* releasing any of these modifiers picks the selection */
	XModifierKeymap *modmap;
	int start_x, start_y;
	Bool pointer_moved;
} ov;


static Bool isListable(WScreen *scr, WWindow *wwin)
{
	if (!wwin->frame || wwin->flags.internal_window || WFLAGP(wwin, skip_window_list))
		return False;
	if (wwin->transient_for != None && wwin->transient_for != scr->root_win)
		return False;
	if (wwin->frame->workspace != scr->current_workspace && !IS_OMNIPRESENT(wwin))
		return False;
	return wwin->flags.mapped || wwin->flags.miniaturized || wwin->flags.hidden || wwin->flags.shaded;
}

static int compareEntries(const void *a, const void *b)
{
	const OverviewEntry *e1 = a, *e2 = b;
	WWindow *w1 = e1->wwin, *w2 = e2->wwin;
	int away1 = w1->flags.miniaturized || w1->flags.hidden;
	int away2 = w2->flags.miniaturized || w2->flags.hidden;
	int c1, c2;

	/* open windows come first, in reading order of their position */
	if (away1 != away2)
		return away1 - away2;
	c1 = w1->frame_y + w1->frame->core->height / 2;
	c2 = w2->frame_y + w2->frame->core->height / 2;
	if (c1 != c2)
		return c1 - c2;
	return (w1->frame_x + w1->frame->core->width / 2) - (w2->frame_x + w2->frame->core->width / 2);
}

static RImage *iconForWindow(WWindow *wwin)
{
	WApplication *wapp;

	if (wwin->net_icon_image)
		return RRetainImage(wwin->net_icon_image);

	wapp = wApplicationOf(wwin->main_window);
	if (wapp && wapp->app_icon && wapp->app_icon->icon && wapp->app_icon->icon->file_image)
		return RRetainImage(wapp->app_icon->icon->file_image);

	return NULL;
}

static char *labelForWindow(WWindow *wwin)
{
	const char *title = "";

	if (wwin->frame && wwin->frame->title)
		title = wwin->frame->title;
	else if (wwin->wm_instance)
		title = wwin->wm_instance;

	if (wwin->flags.miniaturized)
		return wstrconcat(title, _(" (minimized)"));
	if (wwin->flags.hidden)
		return wstrconcat(title, _(" (hidden)"));
	return wstrdup(title);
}

static void initEntry(OverviewEntry *e, WWindow *wwin)
{
	memset(e, 0, sizeof(*e));
	e->wwin = wwin;
	e->client_win = wwin->client_win;
	e->away = !wwin->flags.mapped;
	e->wm_class = wwin->wm_class ? wstrdup(wwin->wm_class) : NULL;
	e->label = labelForWindow(wwin);
	e->nat_w = WMAX(1, wwin->client.width);
	e->nat_h = WMAX(1, wwin->client.height);
}

static void loadEntryImage(OverviewEntry *e)
{
	WWindow *wwin = e->wwin;
	Bool open = !wwin->flags.miniaturized && !wwin->flags.hidden;

	if (open && wSnapshotUsesComposite() && wSnapshotCanGrab(wwin, None))
		e->live = True;

	/* a fresh full size grab is only correct when nothing covers the window */
	if (!e->live && open && wSnapshotCanGrab(wwin, None)) {
		e->image = wSnapshotGrabWindow(wwin, False);
		if (e->image) {
			e->fresh = True;
			e->nat_w = e->image->width;
			e->nat_h = e->image->height;
			return;
		}
	}
	if (wwin->snapshot) {
		e->image = RRetainImage(wwin->snapshot);
		return;
	}
	e->image = iconForWindow(wwin);
	e->is_icon = (e->image != NULL);
}

static void collectEntries(WScreen *scr)
{
	WWindow *wwin;

	ov.count = 0;
	for (wwin = scr->focused_window; wwin && ov.count < MAX_ENTRIES; wwin = wwin->prev) {
		if (!isListable(scr, wwin))
			continue;
		initEntry(&ov.entries[ov.count], wwin);
		loadEntryImage(&ov.entries[ov.count++]);
	}

	qsort(ov.entries, ov.count, sizeof(ov.entries[0]), compareEntries);
}

/* The windows of the switch panel, in the same (most recently focused first) order */
static void collectCycleEntries(WScreen *scr, Bool class_only)
{
	WMArray *list = wSwitchPanelWindowList(scr, class_only);
	WWindow *wwin;
	int i;

	ov.count = 0;
	WM_ITERATE_ARRAY(list, wwin, i) {
		if (ov.count >= MAX_ENTRIES)
			break;
		initEntry(&ov.entries[ov.count++], wwin);
	}
	WMFreeArray(list);
}

static Bool sameClass(OverviewEntry *a, OverviewEntry *b)
{
	return a->wm_class && b->wm_class && strcmp(a->wm_class, b->wm_class) == 0;
}

/* Next window in cycling order, the way the switch panel picks it */
static int cycleStep(int from, Bool back, Bool ignore_minimized, Bool group)
{
	int i = from;

	if (ov.count == 0)
		return from;
	if (!wPreferences.cycle_ignore_minimized || ov.entries[from].away)
		ignore_minimized = False;

	do {
		do {
			i = (i + (back ? -1 : 1) + ov.count) % ov.count;
		} while (group && i != from && !sameClass(&ov.entries[i], &ov.entries[from]));
	} while (ignore_minimized && i != from && ov.entries[i].away);

	return i;
}

static void fitSize(int nat_w, int nat_h, int max_w, int max_h, int *w, int *h)
{
	double f = WMIN((double) max_w / nat_w, (double) max_h / nat_h);

	if (f > 1.0)
		f = 1.0;
	*w = WMAX(1, (int) (nat_w * f));
	*h = WMAX(1, (int) (nat_h * f));
}

/* Pick the number of columns giving the largest thumbnails, then place them */
static void layoutEntries(void)
{
	int area_w = ov.rect.size.width - 2 * OUTER_MARGIN;
	int area_h = ov.rect.size.height - 2 * OUTER_MARGIN;
	int best_cols = 1, cols, rows, cell_w, cell_h, row, i;
	double best_score = -1.0;

	for (cols = 1; cols <= ov.count; cols++) {
		double score = 0.0;

		rows = (ov.count + cols - 1) / cols;
		cell_w = (area_w - (cols - 1) * CELL_GAP) / cols;
		cell_h = (area_h - (rows - 1) * CELL_GAP) / rows - ov.title_height;
		if (cell_w < 16 || cell_h < 16)
			break;

		for (i = 0; i < ov.count; i++) {
			int w, h;

			fitSize(ov.entries[i].nat_w, ov.entries[i].nat_h, cell_w, cell_h, &w, &h);
			score += (double) w * h;
		}
		if (score > best_score) {
			best_score = score;
			best_cols = cols;
		}
	}

	cols = best_cols;
	rows = (ov.count + cols - 1) / cols;
	cell_w = WMAX(16, (area_w - (cols - 1) * CELL_GAP) / cols);
	cell_h = WMAX(16, (area_h - (rows - 1) * CELL_GAP) / rows - ov.title_height);

	for (row = 0, i = 0; row < rows; row++) {
		int in_row = WMIN(cols, ov.count - i);
		int row_x = (area_w - (in_row * cell_w + (in_row - 1) * CELL_GAP)) / 2;
		int grid_y = (area_h - (rows * (cell_h + ov.title_height) + (rows - 1) * CELL_GAP)) / 2;
		int cell_y = OUTER_MARGIN + grid_y + row * (cell_h + ov.title_height + CELL_GAP) + ov.title_height;
		int col;

		for (col = 0; col < in_row; col++, i++) {
			OverviewEntry *e = &ov.entries[i];
			int cell_x = OUTER_MARGIN + row_x + col * (cell_w + CELL_GAP);

			fitSize(e->nat_w, e->nat_h, cell_w, cell_h, &e->w, &e->h);
			e->x = cell_x + (cell_w - e->w) / 2;
			e->y = cell_y + (cell_h - e->h) / 2;
		}
	}
}

static Pixmap currentWallpaper(WScreen *scr)
{
	Atom type;
	int format;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	Pixmap wallpaper = None;

	if (XGetWindowProperty(dpy, scr->root_win, XInternAtom(dpy, "_XROOTPMAP_ID", False), 0, 1, False,
			       XA_PIXMAP, &type, &format, &nitems, &after, &data) == Success && data) {
		if (type == XA_PIXMAP && format == 32 && nitems == 1)
			wallpaper = *(Pixmap *) data;
		XFree(data);
	}
	return wallpaper;
}

/* Copy of the wallpaper below the head, NULL when there is no usable one */
static RImage *wallpaperImage(WScreen *scr, WMRect rect, Pixmap wallpaper)
{
	Window root;
	int x, y;
	unsigned int w, h, bw, depth;
	XImage *ximg;
	XGCValues gcv;
	GC gc;
	Pixmap tmp;
	RImage *img = NULL;

	/* the property may point to a pixmap that is already gone */
	if (wallpaper == None
	    || !XGetGeometry(dpy, wallpaper, &root, &x, &y, &w, &h, &bw, &depth) || depth != scr->w_depth)
		return NULL;

	/* the wallpaper may be tiled, let the server repeat it over the head */
	tmp = XCreatePixmap(dpy, scr->root_win, rect.size.width, rect.size.height, scr->w_depth);
	gcv.fill_style = FillTiled;
	gcv.tile = wallpaper;
	gcv.ts_x_origin = -rect.pos.x;
	gcv.ts_y_origin = -rect.pos.y;
	gc = XCreateGC(dpy, tmp, GCFillStyle | GCTile | GCTileStipXOrigin | GCTileStipYOrigin, &gcv);
	XFillRectangle(dpy, tmp, gc, 0, 0, rect.size.width, rect.size.height);
	XFreeGC(dpy, gc);

	ximg = XGetImage(dpy, tmp, 0, 0, rect.size.width, rect.size.height, AllPlanes, ZPixmap);
	XFreePixmap(dpy, tmp);
	if (ximg) {
		img = RCreateImageFromXImage(scr->rcontext, ximg, NULL);
		XDestroyImage(ximg);
	}
	return img;
}

static Pixmap buildBackground(WScreen *scr, WMRect rect, Pixmap wallpaper)
{
	RImage *img = wallpaperImage(scr, rect, wallpaper);
	Pixmap pixmap;
	unsigned char *p;
	long i, n;
	int channels;

	if (!img) {
		RColor color = { 0x50, 0x50, 0x58, 0xff };

		img = RCreateImage(rect.size.width, rect.size.height, False);
		if (!img)
			return None;
		RFillImage(img, &color);
	}

	channels = (img->format == RRGBAFormat) ? 4 : 3;
	n = (long) img->width * img->height;
	for (i = 0, p = img->data; i < n; i++, p += channels) {
		p[0] = (p[0] * DARKEN) >> 8;
		p[1] = (p[1] * DARKEN) >> 8;
		p[2] = (p[2] * DARKEN) >> 8;
	}

	if (!RConvertImage(scr->rcontext, img, &pixmap))
		pixmap = None;
	RReleaseImage(img);
	return pixmap;
}

/*
 * The darkened wallpaper of each head is kept in the server, so opening
 * the overview only needs a server side copy. It is rebuilt when the
 * wallpaper changes, in the background if a shortcut is bound.
 */
static struct {
	WScreen *scr;
	WMRect rect;
	Pixmap wallpaper;	/* _XROOTPMAP_ID the cached image was made from */
	Pixmap pixmap;
} bg_cache[MAX_CACHED_HEADS];

static WMHandlerID prepare_timer;

static void dropBackground(int i)
{
	if (bg_cache[i].pixmap)
		XFreePixmap(dpy, bg_cache[i].pixmap);
	memset(&bg_cache[i], 0, sizeof(bg_cache[i]));
}

static Pixmap cachedBackground(WScreen *scr, WMRect rect)
{
	Pixmap wallpaper = currentWallpaper(scr);
	int i, slot = -1;

	for (i = 0; i < MAX_CACHED_HEADS; i++) {
		if (bg_cache[i].scr == scr && bg_cache[i].rect.pos.x == rect.pos.x
		    && bg_cache[i].rect.pos.y == rect.pos.y && bg_cache[i].rect.size.width == rect.size.width
		    && bg_cache[i].rect.size.height == rect.size.height) {
			if (bg_cache[i].pixmap && bg_cache[i].wallpaper == wallpaper)
				return bg_cache[i].pixmap;
			slot = i;
			break;
		}
		if (slot < 0 && !bg_cache[i].pixmap)
			slot = i;
	}
	if (slot < 0)
		slot = 0;	/* heads changed a lot, recycle */

	dropBackground(slot);
	bg_cache[slot].pixmap = buildBackground(scr, rect, wallpaper);
	if (!bg_cache[slot].pixmap)
		return None;
	bg_cache[slot].scr = scr;
	bg_cache[slot].rect = rect;
	bg_cache[slot].wallpaper = wallpaper;
	return bg_cache[slot].pixmap;
}

static void prepareBackgrounds(void *data)
{
	WScreen *scr = data;
	int head;

	prepare_timer = NULL;
	if (wKeyBindings[WKBD_WINDOWOVERVIEW].keycode == 0)
		return;
	for (head = 0; head < wXineramaHeads(scr) && head < MAX_CACHED_HEADS; head++)
		cachedBackground(scr, wGetRectForHead(scr, head));
}

static void schedulePrepare(WScreen *scr)
{
	if (prepare_timer)
		WMDeleteTimerHandler(prepare_timer);
	prepare_timer = WMAddTimerHandler(PREPARE_DELAY, prepareBackgrounds, scr);
}

void wWindowOverviewInit(WScreen *scr)
{
	schedulePrepare(scr);
}

void wWindowOverviewPropertyNotify(XPropertyEvent *event)
{
	static Atom rootpmap = None;
	WScreen *scr;
	int i;

	if (rootpmap == None)
		rootpmap = XInternAtom(dpy, "_XROOTPMAP_ID", False);
	if (event->atom != rootpmap)
		return;
	scr = wScreenForRootWindow(event->window);
	if (!scr || scr->root_win != event->window)
		return;

	/* the same pixmap may have been redrawn, so drop the cache even if the id is unchanged */
	for (i = 0; i < MAX_CACHED_HEADS; i++)
		if (bg_cache[i].scr == scr)
			dropBackground(i);
	schedulePrepare(scr);
}

static void putImage(WScreen *scr, Pixmap dest, RImage *img, int x, int y)
{
	RColor dark = { 0x30, 0x30, 0x34, 0xff };

	if (img->format == RRGBAFormat)
		RCombineImageWithColor(img, &dark);
	wSnapshotDraw(scr, dest, img, x, y);
}

static void renderEntry(WScreen *scr, Pixmap dest, OverviewEntry *e, unsigned long fill)
{
	RImage *scaled;

	if (e->live) {
		if (wSnapshotRenderScaled(e->wwin, dest, e->x, e->y, e->w, e->h))
			return;
		e->live = False;
	}

	if (e->image && !e->is_icon) {
		scaled = wSnapshotScale(e->image, e->w, e->h);
		if (scaled) {
			putImage(scr, dest, scaled, e->x, e->y);
			/* the thumbnail is usually large enough to make the cached snapshot from */
			if (e->fresh)
				wSnapshotStore(e->wwin, (scaled->width >= SNAPSHOT_MAX_WIDTH
							 || scaled->height >= SNAPSHOT_MAX_HEIGHT) ? scaled : e->image);
			RReleaseImage(scaled);
			return;
		}
	}

	/* no snapshot: draw a window shaped placeholder with the icon in the middle */
	XSetForeground(dpy, scr->draw_gc, fill);
	XFillRectangle(dpy, dest, scr->draw_gc, e->x, e->y, e->w, e->h);
	if (e->image) {
		int size = WMIN(ICON_SIZE, WMIN(e->w, e->h));

		scaled = wSnapshotScaleToFit(e->image, size, size);
		if (scaled) {
			putImage(scr, dest, scaled, e->x + (e->w - scaled->width) / 2,
				 e->y + (e->h - scaled->height) / 2);
			RReleaseImage(scaled);
		}
	}
}

static Pixmap renderOverview(WScreen *scr)
{
	WMColor *white, *fill;
	Pixmap bg, pixmap;
	int i;

	bg = cachedBackground(scr, ov.rect);
	if (bg == None)
		return None;

	pixmap = XCreatePixmap(dpy, scr->root_win, ov.rect.size.width, ov.rect.size.height, scr->w_depth);
	XCopyArea(dpy, bg, pixmap, scr->draw_gc, 0, 0, ov.rect.size.width, ov.rect.size.height, 0, 0);

	fill = WMCreateRGBColor(scr->wmscreen, 0x3000, 0x3000, 0x3400, True);
	for (i = 0; i < ov.count; i++)
		renderEntry(scr, pixmap, &ov.entries[i], WMColorPixel(fill));
	WMReleaseColor(fill);

	white = WMWhiteColor(scr->wmscreen);
	XSetForeground(dpy, scr->draw_gc, scr->black_pixel);
	for (i = 0; i < ov.count; i++) {
		OverviewEntry *e = &ov.entries[i];
		int max_w = WMAX(e->w, 160);
		char *title = ShrinkString(ov.font, e->label, max_w);
		int len = strlen(title);
		int tw = WMWidthOfString(ov.font, title, len);

		WMDrawString(scr->wmscreen, pixmap, white, ov.font, e->x + (e->w - tw) / 2,
			     e->y - ov.title_height, title, len);
		wfree(title);
		XDrawRectangle(dpy, pixmap, scr->draw_gc, e->x - 1, e->y - 1, e->w + 1, e->h + 1);
	}
	WMReleaseColor(white);

	return pixmap;
}

static int entryAt(int x, int y)
{
	int i, m = FRAME_GAP + FRAME_WIDTH;

	for (i = 0; i < ov.count; i++) {
		OverviewEntry *e = &ov.entries[i];

		if (x >= e->x - m && x < e->x + e->w + m
		    && y >= e->y - ov.title_height && y < e->y + e->h + m)
			return i;
	}
	return -1;
}

static void drawSelection(int index, Bool on)
{
	OverviewEntry *e;
	int m = FRAME_GAP + FRAME_WIDTH;
	int i;

	if (index < 0 || index >= ov.count)
		return;
	e = &ov.entries[index];

	if (!on) {
		XClearArea(dpy, ov.win, e->x - m, e->y - m, e->w + 2 * m, m, False);
		XClearArea(dpy, ov.win, e->x - m, e->y + e->h, e->w + 2 * m, m, False);
		XClearArea(dpy, ov.win, e->x - m, e->y, m, e->h, False);
		XClearArea(dpy, ov.win, e->x + e->w, e->y, m, e->h, False);
		return;
	}

	XSetForeground(dpy, ov.scr->draw_gc, ov.scr->white_pixel);
	for (i = 0; i < FRAME_WIDTH; i++) {
		int d = FRAME_GAP + i + 1;

		XDrawRectangle(dpy, ov.win, ov.scr->draw_gc, e->x - d, e->y - d,
			       e->w + 2 * d - 1, e->h + 2 * d - 1);
	}
}

static void selectEntry(int index)
{
	if (index == ov.selected || index < 0 || index >= ov.count)
		return;
	drawSelection(ov.selected, False);
	ov.selected = index;
	drawSelection(ov.selected, True);
}

/* Closest entry in the given direction, used for arrow key navigation */
static int neighbour(int from, int dx, int dy)
{
	OverviewEntry *f = &ov.entries[from];
	int fx = f->x + f->w / 2, fy = f->y + f->h / 2;
	int best = -1, best_primary = 0, best_perp = 0;
	int i;

	for (i = 0; i < ov.count; i++) {
		OverviewEntry *e = &ov.entries[i];
		int ox = e->x + e->w / 2 - fx, oy = e->y + e->h / 2 - fy;
		int primary = dx ? ox * dx : oy * dy;
		int perp = abs(dx ? oy : ox);

		if (i == from || primary <= 0)
			continue;
		/* left/right stay in the same row */
		if (dx && perp > f->h / 2 + CELL_GAP)
			continue;
		if (best < 0 || primary < best_primary || (primary == best_primary && perp < best_perp)) {
			best = i;
			best_primary = primary;
			best_perp = perp;
		}
	}

	/* rows are not aligned vertically: prefer the closest one in the next row */
	if (best >= 0 && dy) {
		int row_y = ov.entries[best].y + ov.entries[best].h / 2;

		for (i = 0; i < ov.count; i++) {
			OverviewEntry *e = &ov.entries[i];

			if (abs(e->y + e->h / 2 - row_y) <= CELL_GAP
			    && abs(e->x + e->w / 2 - fx) < abs(ov.entries[best].x + ov.entries[best].w / 2 - fx))
				best = i;
		}
	}

	if (best < 0 && dx)
		best = (from + dx + ov.count) % ov.count;
	return best;
}

static void handleKey(XEvent *ev)
{
	WShortKey *key = &wKeyBindings[WKBD_WINDOWOVERVIEW];
	int modifiers = ev->xkey.state & w_global.shortcut.modifiers_mask;
	KeySym ks;

	if (key->keycode != 0 && key->keycode == ev->xkey.keycode && key->modifier == modifiers) {
		ov.done = True;
		return;
	}

	XLookupString(&ev->xkey, NULL, 0, &ks, NULL);
	switch (ks) {
	case XK_Escape:
		ov.done = True;
		break;
	case XK_Return:
	case XK_KP_Enter:
	case XK_space:
		ov.chosen = ov.entries[ov.selected].client_win;
		ov.done = True;
		break;
	case XK_Left:
	case XK_KP_Left:
	case XK_h:
		selectEntry(neighbour(ov.selected, -1, 0));
		break;
	case XK_Right:
	case XK_KP_Right:
	case XK_l:
		selectEntry(neighbour(ov.selected, 1, 0));
		break;
	case XK_Up:
	case XK_KP_Up:
	case XK_k:
		selectEntry(neighbour(ov.selected, 0, -1));
		break;
	case XK_Down:
	case XK_KP_Down:
	case XK_j:
		selectEntry(neighbour(ov.selected, 0, 1));
		break;
	case XK_Tab:
		if (ev->xkey.state & ShiftMask)
			selectEntry((ov.selected + ov.count - 1) % ov.count);
		else
			selectEntry((ov.selected + 1) % ov.count);
		break;
	case XK_ISO_Left_Tab:
		selectEntry((ov.selected + ov.count - 1) % ov.count);
		break;
	case XK_Home:
		selectEntry(0);
		break;
	case XK_End:
		selectEntry(ov.count - 1);
		break;
	default:
		break;
	}
}

static Bool keyMatches(int kbd, XEvent *ev)
{
	int modifiers = ev->xkey.state & w_global.shortcut.modifiers_mask;

	return wKeyBindings[kbd].keycode != 0 && wKeyBindings[kbd].keycode == ev->xkey.keycode
		&& wKeyBindings[kbd].modifier == modifiers;
}

static void handleCycleKey(XEvent *ev)
{
	Bool group_next = !ov.class_only && keyMatches(WKBD_GROUPNEXT, ev);
	Bool group_prev = !ov.class_only && keyMatches(WKBD_GROUPPREV, ev);

	if (keyMatches(WKBD_FOCUSNEXT, ev) || keyMatches(WKBD_GROUPNEXT, ev))
		selectEntry(cycleStep(ov.selected, False, True, group_next));
	else if (keyMatches(WKBD_FOCUSPREV, ev) || keyMatches(WKBD_GROUPPREV, ev))
		selectEntry(cycleStep(ov.selected, True, True, group_prev));
	else
		handleKey(ev);
}

/* Modifier bits a key contributes to, used to notice the release of Alt */
static unsigned int modifiersOfKey(KeyCode keycode)
{
	unsigned int mask = 0;
	int i, j;

	if (!ov.modmap)
		return 0;
	for (i = 0; i < 8; i++)
		for (j = 0; j < ov.modmap->max_keypermod; j++)
			if (ov.modmap->modifiermap[i * ov.modmap->max_keypermod + j] == keycode)
				mask |= 1 << i;
	return mask;
}

static void eventLoop(void)
{
	XEvent ev;

	ov.done = False;
	ov.pressed = -1;
	while (!ov.done) {
		WMMaskEvent(dpy, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask
			    | PointerMotionMask | ExposureMask, &ev);

		if (ev.xany.window != ov.win && ev.type != KeyPress && ev.type != KeyRelease) {
			WMHandleEvent(&ev);
			continue;
		}

		switch (ev.type) {
		case KeyPress:
			if (ov.cycling)
				handleCycleKey(&ev);
			else
				handleKey(&ev);
			break;

		case KeyRelease:
			if (ov.cycling && (modifiersOfKey(ev.xkey.keycode) & ov.hold_mask)) {
				ov.chosen = ov.entries[ov.selected].client_win;
				ov.done = True;
			}
			break;

		case MotionNotify:
			while (XCheckTypedWindowEvent(dpy, ov.win, MotionNotify, &ev))
				;
			/* while cycling, a slight bump of the mouse must not change the selection */
			if (ov.cycling && !ov.pointer_moved) {
				if (abs(ev.xmotion.x - ov.start_x) < HOVER_THRESHOLD
				    && abs(ev.xmotion.y - ov.start_y) < HOVER_THRESHOLD)
					break;
				ov.pointer_moved = True;
			}
			selectEntry(entryAt(ev.xmotion.x, ev.xmotion.y));
			break;

		case ButtonPress:
			if (ev.xbutton.button == Button4)
				selectEntry((ov.selected + ov.count - 1) % ov.count);
			else if (ev.xbutton.button == Button5)
				selectEntry((ov.selected + 1) % ov.count);
			else
				ov.pressed = (ev.xbutton.button == Button1) ? entryAt(ev.xbutton.x, ev.xbutton.y) : -1;
			break;

		case ButtonRelease:
			if (ev.xbutton.button == Button4 || ev.xbutton.button == Button5)
				break;
			/* activate on release so the client does not get a stray release */
			if (ev.xbutton.button == Button1 && ov.pressed >= 0
			    && ov.pressed == entryAt(ev.xbutton.x, ev.xbutton.y))
				ov.chosen = ov.entries[ov.pressed].client_win;
			ov.done = True;
			break;

		case Expose:
			if (ev.xexpose.count == 0)
				drawSelection(ov.selected, True);
			break;
		}
	}
}

static void releaseEntries(void)
{
	int i;

	for (i = 0; i < ov.count; i++) {
		if (ov.entries[i].image)
			RReleaseImage(ov.entries[i].image);
		if (ov.entries[i].wm_class)
			wfree(ov.entries[i].wm_class);
		wfree(ov.entries[i].label);
	}
	ov.count = 0;
}

static Bool beginOverview(WScreen *scr)
{
	memset(&ov, 0, sizeof(ov));
	ov.scr = scr;
	ov.rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
	ov.font = WMBoldSystemFontOfSize(scr->wmscreen, FONT_SIZE);
	if (!ov.font)
		return False;
	ov.title_height = WMFontHeight(ov.font) + TITLE_GAP;
	return True;
}

static void endOverview(void)
{
	releaseEntries();
	WMReleaseFont(ov.font);
	ov.font = NULL;
	if (ov.modmap)
		XFreeModifiermap(ov.modmap);
	ov.modmap = NULL;
}

/* Show the collected and laid out entries and let the user pick one */
static void runOverview(WScreen *scr)
{
	XSetWindowAttributes attribs;
	Pixmap pixmap;
	int i;

	pixmap = renderOverview(scr);
	if (pixmap == None)
		return;

	attribs.override_redirect = True;
	attribs.background_pixmap = pixmap;
	attribs.colormap = scr->w_colormap;
	attribs.border_pixel = 0;
	attribs.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask
		| PointerMotionMask | ExposureMask;
	ov.win = XCreateWindow(dpy, scr->root_win, ov.rect.pos.x, ov.rect.pos.y,
			       ov.rect.size.width, ov.rect.size.height, 0, scr->w_depth, InputOutput,
			       scr->w_visual, CWOverrideRedirect | CWBackPixmap | CWColormap
			       | CWBorderPixel | CWEventMask, &attribs);
	XFreePixmap(dpy, pixmap);
	XMapRaised(dpy, ov.win);
	XFlush(dpy);

	/* the overview is on screen, now refresh the cached snapshots of live windows */
	for (i = 0; i < ov.count; i++)
		if (ov.entries[i].live)
			wSnapshotUpdate(ov.entries[i].wwin, False);

	if (XGrabKeyboard(dpy, ov.win, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
		Window root, child;
		int rx, ry;
		unsigned int mask;

		XGrabPointer(dpy, ov.win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
			     GrabModeAsync, GrabModeAsync, ov.win, None, CurrentTime);
		XQueryPointer(dpy, ov.win, &root, &child, &rx, &ry, &ov.start_x, &ov.start_y, &mask);
		drawSelection(ov.selected, True);
		eventLoop();
		XUngrabPointer(dpy, CurrentTime);
		XUngrabKeyboard(dpy, CurrentTime);
	} else {
		wwarning(_("could not grab the keyboard for the window overview"));
	}

	XDestroyWindow(dpy, ov.win);
	ov.win = None;
}

void StartWindowOverview(WScreen *scr)
{
	WWindow *wwin;
	Window chosen;
	int i;

	if (ov.win)
		return;		/* already running */

	wWinPreviewHide();

	if (!beginOverview(scr))
		return;

	collectEntries(scr);
	if (ov.count == 0) {
		endOverview();
		return;
	}
	layoutEntries();

	ov.selected = 0;
	for (i = 0; i < ov.count; i++) {
		if (ov.entries[i].wwin == scr->focused_window) {
			ov.selected = i;
			break;
		}
	}

	runOverview(scr);
	chosen = ov.chosen;
	endOverview();

	if (chosen != None) {
		wwin = wWindowFor(chosen);
		if (wwin && wwin->client_win == chosen)
			wWinPreviewActivate(wwin);
	}
	XFlush(dpy);
}

Bool wWindowOverviewCycle(WWindow *wwin, XEvent *event, Bool next, Bool class_only)
{
	WScreen *scr = wScreenForRootWindow(event->xkey.root);
	WShortKey binding;
	Window root, child, chosen;
	int rx, ry, wx, wy, i;
	unsigned int mask, hold;

	if (ov.win || !scr)
		return False;

	if (class_only)
		binding = wKeyBindings[next ? WKBD_GROUPNEXT : WKBD_GROUPPREV];
	else
		binding = wKeyBindings[next ? WKBD_FOCUSNEXT : WKBD_FOCUSPREV];

	/* Shift only reverses the direction, it is not the key being held */
	hold = binding.modifier;
	if (hold & ~ShiftMask)
		hold &= ~ShiftMask;
	if (hold == 0)
		return False;

	if (XGrabKeyboard(dpy, scr->root_win, False, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
		return False;

	wWinPreviewHide();
	if (!beginOverview(scr)) {
		XUngrabKeyboard(dpy, CurrentTime);
		return False;
	}
	ov.cycling = True;
	ov.class_only = class_only;
	ov.hold_mask = hold;
	ov.modmap = XGetModifierMapping(dpy);

	collectCycleEntries(scr, class_only);
	if (ov.count == 0) {
		endOverview();
		XUngrabKeyboard(dpy, CurrentTime);
		return False;
	}

	/* preselect what the switch panel would */
	ov.selected = 0;
	if (wwin->flags.mapped && !wPreferences.panel_only_open)
		ov.selected = cycleStep(0, !next, True, False);

	scr->flags.doing_alt_tab = 1;

	/* a quick Alt+Tab switches without showing anything */
	XQueryPointer(dpy, scr->root_win, &root, &child, &rx, &ry, &wx, &wy, &mask);
	if (mask & hold) {
		for (i = 0; i < ov.count; i++)
			loadEntryImage(&ov.entries[i]);
		layoutEntries();
		runOverview(scr);
		chosen = ov.chosen;
	} else {
		chosen = ov.entries[ov.selected].client_win;
	}
	endOverview();
	XUngrabKeyboard(dpy, CurrentTime);

	/* focus it the way the switch panel does */
	wwin = (chosen != None) ? wWindowFor(chosen) : NULL;
	if (wwin && wwin->client_win == chosen) {
		wRaiseFrame(wwin->frame->core);
		CommitStacking(scr);
		if (!wwin->flags.mapped)
			wMakeWindowVisible(wwin);
		wSetFocusTo(scr, wwin);
	}

	scr->flags.doing_alt_tab = 0;
	XFlush(dpy);
	return True;
}
