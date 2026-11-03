/* winpreview.c - popup with snapshots of an application's minimized/hidden windows
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
#include <string.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "texture.h"
#include "icon.h"
#include "appicon.h"
#include "actions.h"
#include "stacking.h"
#include "workspace.h"
#include "xinerama.h"
#include "misc.h"
#include "snapshot.h"
#include "winpreview.h"


#define SHOW_DELAY		400	/* ms the pointer must rest on the icon */
#define HIDE_DELAY		300	/* ms allowed to travel from the icon to the popup */
#define PAD			6	/* space between the popup edge and the cells */
#define GAP			8	/* space between cells */
#define ANCHOR_GAP		4	/* space between the icon and the popup */
#define HIGHLIGHT		3	/* distance of the hover frame from the cell */
#define MIN_CELL_WIDTH		96
#define MIN_IMAGE_HEIGHT	32
#define FALLBACK_ICON_SIZE	64
#define MAX_ENTRIES		32

typedef struct {
	Window client_win;
	int x, y, w, h;
} PreviewCell;

static struct {
	WScreen *scr;
	Window win;
	WObjDescriptor desc;
	int width, height;
	Bool mapped;

	Window anchor;			/* icon window the popup belongs to */
	Window leader;			/* list all windows of this application... */
	Window single;			/* ...or only this window (miniwindows) */

	PreviewCell *cells;
	int count;
	int hover;

	WMHandlerID show_timer;
	WMHandlerID hide_timer;
} popup = { .hover = -1 };


static int collectWindows(WScreen *scr, Window leader, Window single, WWindow **list, int max)
{
	WWindow *wwin;
	int n = 0;

	if (single != None) {
		wwin = wWindowFor(single);
		if (!wwin || wwin->client_win != single)
			return 0;
		if (list && max > 0)
			list[0] = wwin;
		return 1;
	}

	if (leader == None)
		return 0;

	for (wwin = scr->focused_window; wwin && n < max; wwin = wwin->prev) {
		if (wwin->main_window != leader || wwin->flags.internal_window)
			continue;
		if (WFLAGP(wwin, skip_window_list))
			continue;
		/* transients come back together with their owner */
		if (wwin->transient_for != None && wwin->transient_for != scr->root_win)
			continue;
		if (list)
			list[n] = wwin;
		n++;
	}
	return n;
}

static Bool isVisible(WScreen *scr, WWindow *wwin)
{
	return !wwin->flags.miniaturized && !wwin->flags.hidden && wwin->flags.mapped
		&& wwin->frame->workspace == scr->current_workspace;
}

/* Snapshots of visible windows go stale, refresh those that can be read */
static void refreshVisibleSnapshots(WScreen *scr, WWindow **list, int count)
{
	int i;

	for (i = 0; i < count; i++)
		if (isVisible(scr, list[i]) && wSnapshotCanGrab(list[i], popup.win))
			wSnapshotUpdate(list[i], False);
}

/* Size of a snapshot of the window, the server can scale it directly to the popup */
static void liveSize(WWindow *wwin, int *w, int *h)
{
	int cw = WMAX(1, (int) wwin->client.width), ch = WMAX(1, (int) wwin->client.height);
	double f = WMIN(1.0, WMIN((double) SNAPSHOT_MAX_WIDTH / cw, (double) SNAPSHOT_MAX_HEIGHT / ch));

	*w = WMAX(1, (int) (cw * f + 0.5));
	*h = WMAX(1, (int) (ch * f + 0.5));
}

static WAppIcon *appIconForWindow(Window win)
{
	WObjDescriptor *desc;

	if (win == None)
		return NULL;
	if (XFindContext(dpy, win, w_global.context.client_win, (XPointer *) &desc) == XCNOENT)
		return NULL;
	if (desc->parent_type != WCLASS_APPICON && desc->parent_type != WCLASS_DOCK_ICON)
		return NULL;
	return desc->parent;
}

static RImage *imageForWindow(WWindow *wwin, WAppIcon *aicon)
{
	RImage *src = NULL;

	if (wwin->snapshot)
		return wSnapshotScaleToFit(wwin->snapshot, SNAPSHOT_MAX_WIDTH, SNAPSHOT_MAX_HEIGHT);

	if (wwin->net_icon_image)
		src = wwin->net_icon_image;
	else if (aicon && aicon->icon && aicon->icon->file_image)
		src = aicon->icon->file_image;

	return src ? wSnapshotScaleToFit(src, FALLBACK_ICON_SIZE, FALLBACK_ICON_SIZE) : NULL;
}

static const char *titleForWindow(WWindow *wwin)
{
	if (wwin->frame && wwin->frame->title)
		return wwin->frame->title;
	if (wwin->wm_instance)
		return wwin->wm_instance;
	return "";
}

static int cellAt(int x, int y)
{
	int i;

	for (i = 0; i < popup.count; i++) {
		PreviewCell *c = &popup.cells[i];

		if (x >= c->x - HIGHLIGHT && x < c->x + c->w + HIGHLIGHT
		    && y >= c->y - HIGHLIGHT && y < c->y + c->h + HIGHLIGHT)
			return i;
	}
	return -1;
}

static void drawHover(int index, Bool on)
{
	PreviewCell *c;
	GC gc = popup.scr->draw_gc;
	int x, y, w, h;

	if (index < 0 || index >= popup.count)
		return;

	c = &popup.cells[index];
	x = c->x - HIGHLIGHT;
	y = c->y - HIGHLIGHT;
	w = c->w + 2 * HIGHLIGHT - 1;
	h = c->h + 2 * HIGHLIGHT - 1;

	if (on) {
		XSetForeground(dpy, gc, popup.scr->white_pixel);
		XDrawRectangle(dpy, popup.win, gc, x, y, w, h);
		XDrawRectangle(dpy, popup.win, gc, x + 1, y + 1, w - 2, h - 2);
	} else {
		XClearArea(dpy, popup.win, x, y, w + 1, 2, False);
		XClearArea(dpy, popup.win, x, y + h - 1, w + 1, 2, False);
		XClearArea(dpy, popup.win, x, y, 2, h + 1, False);
		XClearArea(dpy, popup.win, x + w - 1, y, 2, h + 1, False);
	}
}

static void setHover(int index)
{
	if (index == popup.hover)
		return;
	drawHover(popup.hover, False);
	popup.hover = index;
	drawHover(popup.hover, True);
}

static void cancelShow(void)
{
	if (popup.show_timer) {
		WMDeleteTimerHandler(popup.show_timer);
		popup.show_timer = NULL;
	}
}

static void cancelHide(void)
{
	if (popup.hide_timer) {
		WMDeleteTimerHandler(popup.hide_timer);
		popup.hide_timer = NULL;
	}
}

static void hideTimeout(void *data)
{
	(void) data;

	popup.hide_timer = NULL;
	wWinPreviewHide();
}

static void scheduleHide(void)
{
	if (!popup.hide_timer)
		popup.hide_timer = WMAddTimerHandler(HIDE_DELAY, hideTimeout, NULL);
}

static void handleMouseDown(WObjDescriptor *desc, XEvent *event)
{
	Window client_win = None;
	WWindow *wwin;
	int index;

	(void) desc;

	index = cellAt(event->xbutton.x, event->xbutton.y);
	if (event->xbutton.button == Button1 && index >= 0)
		client_win = popup.cells[index].client_win;

	wWinPreviewHide();

	if (client_win == None)
		return;

	/* the window may have gone away while the popup was open */
	wwin = wWindowFor(client_win);
	if (!wwin || wwin->client_win != client_win)
		return;

	wWinPreviewActivate(wwin);
}

void wWinPreviewActivate(WWindow *wwin)
{
	if (wwin->flags.miniaturized || wwin->flags.hidden) {
		wRestoreWindow(wwin);
		return;
	}

	/* already open: only bring it to the front */
	if (wwin->frame->workspace != wwin->screen_ptr->current_workspace)
		wWorkspaceChange(wwin->screen_ptr, wwin->frame->workspace);
	wRaiseFrame(wwin->frame->core);
	if (!WFLAGP(wwin, no_focusable))
		wSetFocusTo(wwin->screen_ptr, wwin);
}

static void ensureWindow(WScreen *scr)
{
	XSetWindowAttributes attribs;
	unsigned long vmask;

	if (popup.win && popup.scr == scr)
		return;

	if (popup.win) {
		wWinPreviewHide();
		XDeleteContext(dpy, popup.win, w_global.context.client_win);
		XDestroyWindow(dpy, popup.win);
	}

	vmask = CWSaveUnder | CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel | CWEventMask;
	attribs.save_under = True;
	attribs.override_redirect = True;
	attribs.colormap = scr->w_colormap;
	attribs.background_pixel = scr->light_pixel;
	attribs.border_pixel = scr->black_pixel;
	attribs.event_mask = ButtonPressMask | ButtonReleaseMask | EnterWindowMask
		| LeaveWindowMask | PointerMotionMask;

	popup.win = XCreateWindow(dpy, scr->root_win, 0, 0, 10, 10, 1, scr->w_depth,
				  CopyFromParent, scr->w_visual, vmask, &attribs);
	popup.scr = scr;

	popup.desc.self = &popup;
	popup.desc.parent = &popup;
	popup.desc.parent_type = WCLASS_UNKNOWN;
	popup.desc.handle_mousedown = handleMouseDown;
	XSaveContext(dpy, popup.win, w_global.context.client_win, (XPointer) &popup.desc);
}

/* Assign every cell a position, wrapping rows that would not fit on the head */
static void layoutCells(const int *img_w, const int *img_h, int count, int max_width, int title_height)
{
	int row_start = 0, row_y = PAD, x = PAD;
	int i, j, row_img_h;

	popup.width = 0;

	for (i = 0; i <= count; i++) {
		int w = 0;

		if (i < count)
			w = WMAX(img_w[i] ? img_w[i] : FALLBACK_ICON_SIZE, MIN_CELL_WIDTH);

		if (i == count || (i > row_start && x + w + PAD > max_width)) {
			/* close the current row */
			row_img_h = MIN_IMAGE_HEIGHT;
			for (j = row_start; j < i; j++)
				row_img_h = WMAX(row_img_h, img_h[j]);
			for (j = row_start; j < i; j++) {
				popup.cells[j].y = row_y;
				popup.cells[j].h = title_height + row_img_h;
			}
			popup.width = WMAX(popup.width, x - GAP + PAD);
			row_y += title_height + row_img_h + GAP;
			row_start = i;
			x = PAD;
		}

		if (i < count) {
			popup.cells[i].x = x;
			popup.cells[i].w = w;
			x += w + GAP;
		}
	}
	popup.height = row_y - GAP + PAD;
}

static Pixmap renderCells(WWindow **list, RImage **images, const Bool *live,
			  const int *img_w, const int *img_h, int count, int title_height)
{
	WScreen *scr = popup.scr;
	WMFont *font = scr->info_text_font;
	WMColor *fg = scr->black;
	unsigned long bg_pixel = scr->light_pixel;
	RColor bg = { 0xae, 0xaa, 0xae, 0xff };
	Pixmap pixmap;
	int i;

	if (scr->window_title_texture[WS_FOCUSED]) {
		XColor *c = &scr->window_title_texture[WS_FOCUSED]->any.color;

		bg_pixel = c->pixel;
		bg.red = c->red >> 8;
		bg.green = c->green >> 8;
		bg.blue = c->blue >> 8;
		fg = scr->window_title_color[WS_FOCUSED];
	}

	pixmap = XCreatePixmap(dpy, scr->root_win, popup.width, popup.height, scr->w_depth);
	XSetForeground(dpy, scr->draw_gc, bg_pixel);
	XFillRectangle(dpy, pixmap, scr->draw_gc, 0, 0, popup.width, popup.height);

	for (i = 0; i < count; i++) {
		PreviewCell *c = &popup.cells[i];
		char *title = ShrinkString(font, titleForWindow(list[i]), c->w);
		int len = strlen(title);
		int iw = img_w[i], ih = img_h[i];
		int ix = c->x + (c->w - iw) / 2;
		int iy = c->y + title_height + (c->h - title_height - ih) / 2;

		WMDrawString(scr->wmscreen, pixmap, fg, font,
			     c->x + (c->w - WMWidthOfString(font, title, len)) / 2, c->y + 2, title, len);
		wfree(title);

		if (live[i] && wSnapshotRenderScaled(list[i], pixmap, ix, iy, iw, ih)) {
			/* drawn by the server */
		} else if (images[i]) {
			/* the cached image may have another size than the live one */
			ix += (iw - (int) images[i]->width) / 2;
			iy += (ih - (int) images[i]->height) / 2;
			iw = images[i]->width;
			ih = images[i]->height;
			RCombineImageWithColor(images[i], &bg);
			wSnapshotDraw(scr, pixmap, images[i], ix, iy);
		} else {
			continue;
		}
		XSetForeground(dpy, scr->draw_gc, scr->black_pixel);
		XDrawRectangle(dpy, pixmap, scr->draw_gc, ix - 1, iy - 1, iw + 1, ih + 1);
	}

	return pixmap;
}

/* Place the popup above the icon, or below/beside it when there is no room */
static void placePopup(int *px, int *py)
{
	WScreen *scr = popup.scr;
	Window root;
	int ax, ay, x, y;
	unsigned int aw, ah, bw, depth;
	WMRect rect;
	int w = popup.width + 2, h = popup.height + 2;

	if (!XGetGeometry(dpy, popup.anchor, &root, &ax, &ay, &aw, &ah, &bw, &depth)) {
		ax = ay = 0;
		aw = ah = wPreferences.icon_size;
	}
	rect = wGetRectForHead(scr, wGetHeadForPoint(scr, wmkpoint(ax + aw / 2, ay + ah / 2)));

	x = ax + (int) aw / 2 - w / 2;
	if (ay - ANCHOR_GAP - h >= rect.pos.y) {
		y = ay - ANCHOR_GAP - h;
	} else if (ay + (int) ah + ANCHOR_GAP + h <= rect.pos.y + (int) rect.size.height) {
		y = ay + ah + ANCHOR_GAP;
	} else {
		/* tall popup next to an icon in the middle of a vertical dock */
		y = ay + (int) ah / 2 - h / 2;
		if (ax + (int) aw / 2 < rect.pos.x + (int) rect.size.width / 2)
			x = ax + aw + ANCHOR_GAP;
		else
			x = ax - ANCHOR_GAP - w;
	}

	x = WMIN(x, rect.pos.x + (int) rect.size.width - w);
	x = WMAX(x, rect.pos.x);
	y = WMIN(y, rect.pos.y + (int) rect.size.height - h);
	y = WMAX(y, rect.pos.y);

	*px = x;
	*py = y;
}

static void showPopup(void)
{
	WScreen *scr = popup.scr;
	WAppIcon *aicon = appIconForWindow(popup.anchor);
	WWindow *list[MAX_ENTRIES];
	RImage *images[MAX_ENTRIES];
	Bool live[MAX_ENTRIES];
	int img_w[MAX_ENTRIES], img_h[MAX_ENTRIES];
	WMRect rect;
	Pixmap pixmap;
	int count, i, title_height, x, y;

	popup.show_timer = NULL;

	count = collectWindows(scr, popup.leader, popup.single, list, MAX_ENTRIES);
	if (count == 0) {
		wWinPreviewHide();
		return;
	}

	/* with composite the server draws visible windows, otherwise grab them now */
	if (!wSnapshotUsesComposite())
		refreshVisibleSnapshots(scr, list, count);

	for (i = 0; i < count; i++) {
		live[i] = wSnapshotUsesComposite() && isVisible(scr, list[i])
			&& wSnapshotCanGrab(list[i], popup.win);
		images[i] = imageForWindow(list[i], aicon);
		if (live[i]) {
			liveSize(list[i], &img_w[i], &img_h[i]);
		} else {
			img_w[i] = images[i] ? images[i]->width : 0;
			img_h[i] = images[i] ? images[i]->height : 0;
		}
	}

	wfree(popup.cells);
	popup.cells = wmalloc(sizeof(PreviewCell) * count);
	popup.count = count;
	popup.hover = -1;
	for (i = 0; i < count; i++)
		popup.cells[i].client_win = list[i]->client_win;

	rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
	title_height = WMFontHeight(scr->info_text_font) + 4;
	layoutCells(img_w, img_h, count, rect.size.width - 2 * ANCHOR_GAP, title_height);
	pixmap = renderCells(list, images, live, img_w, img_h, count, title_height);

	for (i = 0; i < count; i++)
		if (images[i])
			RReleaseImage(images[i]);

	placePopup(&x, &y);

	XMoveResizeWindow(dpy, popup.win, x, y, popup.width, popup.height);
	XSetWindowBackgroundPixmap(dpy, popup.win, pixmap);
	XFreePixmap(dpy, pixmap);
	XClearWindow(dpy, popup.win);
	XMapRaised(dpy, popup.win);
	XFlush(dpy);
	popup.mapped = True;

	/* keep the cache current for when the windows get minimized or hidden */
	for (i = 0; i < count; i++)
		if (live[i])
			wSnapshotUpdate(list[i], False);
}

static void showTimeout(void *data)
{
	(void) data;

	showPopup();
}

Bool wWinPreviewEnterNotify(WScreen *scr, WObjDescriptor *desc, XEvent *event)
{
	Window win = event->xcrossing.window;
	Window leader = None, single = None, icon_win = None;

	if (popup.mapped && (win == popup.win || win == popup.anchor)) {
		cancelHide();
		return True;
	}

	cancelShow();

	/* ignore crossings caused by grabs, e.g. when the icon menu closes */
	if (event->xcrossing.mode == NotifyNormal && desc) {
		if (desc->parent_type == WCLASS_APPICON || desc->parent_type == WCLASS_DOCK_ICON) {
			WAppIcon *aicon = desc->parent;

			if (aicon && aicon->icon) {
				icon_win = aicon->icon->core->window;
				leader = aicon->main_window;
			}
		} else if (desc->parent_type == WCLASS_MINIWINDOW) {
			WIcon *icon = desc->parent;

			if (icon && icon->owner) {
				icon_win = icon->core->window;
				single = icon->owner->client_win;
			}
		}
	}

	if (icon_win != None && win == icon_win
	    && collectWindows(scr, leader, single, NULL, 1) > 0) {
		cancelHide();
		ensureWindow(scr);
		popup.anchor = win;
		popup.leader = leader;
		popup.single = single;
		if (popup.mapped)
			showPopup();
		else
			popup.show_timer = WMAddTimerHandler(SHOW_DELAY, showTimeout, NULL);
		return True;
	}

	if (popup.mapped)
		scheduleHide();
	return False;
}

void wWinPreviewLeaveNotify(XEvent *event)
{
	Window win = event->xcrossing.window;

	if (event->xcrossing.detail == NotifyInferior)
		return;
	if (win != popup.anchor && win != popup.win)
		return;

	if (popup.show_timer) {
		cancelShow();
		popup.anchor = None;
		popup.leader = None;
		popup.single = None;
	} else if (popup.mapped) {
		if (win == popup.win)
			setHover(-1);
		scheduleHide();
	}
}

Bool wWinPreviewMotionNotify(XEvent *event)
{
	if (!popup.mapped || event->xmotion.window != popup.win)
		return False;

	cancelHide();
	setHover(cellAt(event->xmotion.x, event->xmotion.y));
	return True;
}

void wWinPreviewButtonPress(XEvent *event)
{
	if (popup.win && event->xbutton.window == popup.win)
		return;
	wWinPreviewHide();
}

void wWinPreviewHide(void)
{
	cancelShow();
	cancelHide();

	if (popup.mapped) {
		XUnmapWindow(dpy, popup.win);
		popup.mapped = False;
	}

	wfree(popup.cells);
	popup.cells = NULL;
	popup.count = 0;
	popup.hover = -1;
	popup.anchor = None;
	popup.leader = None;
	popup.single = None;
}

void wWinPreviewForgetWindow(WWindow *wwin)
{
	int i;

	for (i = 0; i < popup.count; i++) {
		if (popup.cells[i].client_win == wwin->client_win) {
			wWinPreviewHide();
			return;
		}
	}
}
