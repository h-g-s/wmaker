/* snapshot.c - capture and cache reduced images of client windows
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
#ifdef USE_COMPOSITE
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#endif

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "snapshot.h"


/* ms after a focus change before the focused window is captured */
#define FOCUS_SNAPSHOT_DELAY	700

static struct {
	WScreen *scr;
	Window client_win;
	WMHandlerID timer;
} focus_capture;


void wSnapshotInitScreen(WScreen *scr)
{
#ifdef USE_COMPOSITE
	if (!w_global.xext.composite.supported || !wPreferences.composite_previews)
		return;

	/*
	 * With automatic redirection the server still draws everything on
	 * screen, but also keeps each top level window's content in its own
	 * pixmap, so covered and partly off-screen windows can be captured.
	 */
	XCompositeRedirectSubwindows(dpy, scr->root_win, CompositeRedirectAutomatic);
	w_global.xext.composite.redirected = True;
#else
	(void) scr;
#endif
}

/*
 * RCreateImageFromXImage() reads every pixel with XGetPixel(), which is
 * slow for window sized images. Convert the usual 32 bpp TrueColor
 * layout directly and leave everything else to wrlib.
 */
static RImage *imageFromXImage(RContext *context, XImage *ximg)
{
	const union { unsigned int i; unsigned char c; } host = { 1 };
	int host_order = host.c ? LSBFirst : MSBFirst;
	unsigned long rmask = ximg->red_mask, gmask = ximg->green_mask, bmask = ximg->blue_mask;
	unsigned char *out;
	RImage *img;
	int x, y;

	if (context->depth == 24 || context->depth == 32) {
		rmask = context->visual->red_mask;
		gmask = context->visual->green_mask;
		bmask = context->visual->blue_mask;
	}

	if (ximg->bits_per_pixel != 32 || ximg->byte_order != host_order
	    || rmask != 0xff0000 || gmask != 0xff00 || bmask != 0xff)
		return RCreateImageFromXImage(context, ximg, NULL);

	img = RCreateImage(ximg->width, ximg->height, False);
	if (!img)
		return NULL;

	out = img->data;
	for (y = 0; y < ximg->height; y++) {
		const unsigned int *row = (const unsigned int *) (ximg->data + y * ximg->bytes_per_line);

		for (x = 0; x < ximg->width; x++) {
			unsigned int p = row[x];

			*out++ = p >> 16;
			*out++ = p >> 8;
			*out++ = p;
		}
	}
	return img;
}

/*
 * Shrink by averaging the block of source pixels that falls on each
 * destination pixel. Much faster than RSmoothScaleImage() when reducing
 * large images and gives similar quality.
 */
static RImage *boxScale(RImage *src, int w, int h)
{
	int channels = (src->format == RRGBAFormat) ? 4 : 3;
	int sw = src->width, sh = src->height;
	unsigned int *acc;
	int *xs;
	unsigned char *out;
	RImage *dst;
	int x, y, c;

	dst = RCreateImage(w, h, channels == 4);
	if (!dst)
		return NULL;
	acc = wmalloc(w * channels * sizeof(unsigned int));
	xs = wmalloc((w + 1) * sizeof(int));

	for (x = 0; x <= w; x++)
		xs[x] = (long) x * sw / w;

	out = dst->data;
	for (y = 0; y < h; y++) {
		int y0 = (long) y * sh / h;
		int y1 = WMAX(y0 + 1, (long) (y + 1) * sh / h);
		int sy;

		memset(acc, 0, w * channels * sizeof(unsigned int));
		for (sy = y0; sy < y1; sy++) {
			const unsigned char *row = src->data + (long) sy * sw * channels;
			unsigned int *a = acc;

			for (x = 0; x < w; x++, a += channels) {
				const unsigned char *p = row + xs[x] * channels;
				const unsigned char *end = row + WMAX(xs[x] + 1, xs[x + 1]) * channels;

				for (; p < end; p += channels)
					for (c = 0; c < channels; c++)
						a[c] += p[c];
			}
		}
		for (x = 0; x < w; x++) {
			unsigned int n = WMAX(1, xs[x + 1] - xs[x]) * (y1 - y0);

			for (c = 0; c < channels; c++)
				*out++ = acc[x * channels + c] / n;
		}
	}

	wfree(acc);
	wfree(xs);
	return dst;
}

static Bool compositeActive(void)
{
#ifdef USE_COMPOSITE
	return w_global.xext.composite.redirected;
#else
	return False;
#endif
}

#ifdef USE_COMPOSITE
/* Read the client area from the off-screen pixmap of the frame */
static RImage *grabComposite(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	Window frame = wwin->frame->core->window;
	XWindowAttributes attr;
	Window child;
	Pixmap pixmap;
	XImage *pimg;
	RImage *img = NULL;
	int x, y, w, h;

	if (!XGetWindowAttributes(dpy, frame, &attr) || attr.map_state != IsViewable)
		return NULL;
	if (attr.depth != scr->w_depth)
		return NULL;
	if (!XTranslateCoordinates(dpy, wwin->client_win, frame, 0, 0, &x, &y, &child))
		return NULL;

	/* the pixmap includes the border of the frame */
	x += attr.border_width;
	y += attr.border_width;
	w = WMIN((int) wwin->client.width, attr.width + 2 * attr.border_width - x);
	h = WMIN((int) wwin->client.height, attr.height + 2 * attr.border_width - y);
	if (x < 0 || y < 0 || w <= 0 || h <= 0)
		return NULL;

	pixmap = XCompositeNameWindowPixmap(dpy, frame);
	if (pixmap == None)
		return NULL;

	pimg = XGetImage(dpy, pixmap, x, y, w, h, AllPlanes, ZPixmap);
	XFreePixmap(dpy, pixmap);
	if (pimg) {
		img = imageFromXImage(scr->rcontext, pimg);
		XDestroyImage(pimg);
	}
	return img;
}
#endif

void wSnapshotDraw(WScreen *scr, Drawable dest, RImage *img, int x, int y)
{
	const union { unsigned int i; unsigned char c; } host = { 1 };
	RContext *ctx = scr->rcontext;
	int channels = (img->format == RRGBAFormat) ? 4 : 3;
	XImage *ximg = NULL;
	Pixmap pixmap;

	/* the usual 24 bit TrueColor visual: build the XImage directly */
	if (ctx->vclass == TrueColor && (ctx->depth == 24 || ctx->depth == 32)
	    && ctx->visual->red_mask == 0xff0000 && ctx->visual->green_mask == 0xff00
	    && ctx->visual->blue_mask == 0xff) {
		ximg = XCreateImage(dpy, ctx->visual, ctx->depth, ZPixmap, 0, NULL,
				    img->width, img->height, 32, 0);
		if (ximg && ximg->bits_per_pixel != 32) {
			XDestroyImage(ximg);
			ximg = NULL;
		}
	}

	if (ximg) {
		const unsigned char *in = img->data;
		unsigned int *out;
		long i, n = (long) img->width * img->height;

		/* Xlib swaps the bytes if the server uses another order */
		ximg->byte_order = host.c ? LSBFirst : MSBFirst;
		ximg->bytes_per_line = img->width * 4;
		ximg->data = wmalloc((size_t) n * 4);
		out = (unsigned int *) ximg->data;
		for (i = 0; i < n; i++, in += channels)
			out[i] = (in[0] << 16) | (in[1] << 8) | in[2];

		XPutImage(dpy, dest, scr->draw_gc, ximg, 0, 0, x, y, img->width, img->height);
		XDestroyImage(ximg);
		return;
	}

	if (RConvertImage(ctx, img, &pixmap)) {
		XCopyArea(dpy, pixmap, dest, scr->draw_gc, 0, 0, img->width, img->height, x, y);
		XFreePixmap(dpy, pixmap);
	}
}

Bool wSnapshotUsesComposite(void)
{
	return compositeActive();
}

Bool wSnapshotRenderScaled(WWindow *wwin, Drawable dest, int x, int y, int w, int h)
{
#ifdef USE_COMPOSITE
	WScreen *scr = wwin->screen_ptr;
	Window frame, child;
	XWindowAttributes attr;
	XRenderPictFormat *sfmt, *dfmt;
	XTransform xf;
	Picture src, dst;
	Pixmap pixmap;
	int cx, cy, cw, ch;

	if (!compositeActive() || !wwin->frame || !wwin->flags.mapped || wwin->flags.shaded || w <= 0 || h <= 0)
		return False;

	frame = wwin->frame->core->window;
	if (!XGetWindowAttributes(dpy, frame, &attr) || attr.map_state != IsViewable)
		return False;
	if (!XTranslateCoordinates(dpy, wwin->client_win, frame, 0, 0, &cx, &cy, &child))
		return False;

	/* the pixmap includes the border of the frame */
	cx += attr.border_width;
	cy += attr.border_width;
	cw = WMIN((int) wwin->client.width, attr.width + 2 * attr.border_width - cx);
	ch = WMIN((int) wwin->client.height, attr.height + 2 * attr.border_width - cy);
	if (cx < 0 || cy < 0 || cw <= 0 || ch <= 0)
		return False;

	sfmt = XRenderFindVisualFormat(dpy, attr.visual);
	dfmt = XRenderFindVisualFormat(dpy, scr->w_visual);
	if (!sfmt || !dfmt)
		return False;

	pixmap = XCompositeNameWindowPixmap(dpy, frame);
	if (pixmap == None)
		return False;

	src = XRenderCreatePicture(dpy, pixmap, sfmt, 0, NULL);
	dst = XRenderCreatePicture(dpy, dest, dfmt, 0, NULL);

	/* map the destination rectangle onto the client area of the frame */
	memset(&xf, 0, sizeof(xf));
	xf.matrix[0][0] = XDoubleToFixed((double) cw / w);
	xf.matrix[0][2] = XDoubleToFixed(cx);
	xf.matrix[1][1] = XDoubleToFixed((double) ch / h);
	xf.matrix[1][2] = XDoubleToFixed(cy);
	xf.matrix[2][2] = XDoubleToFixed(1);
	XRenderSetPictureTransform(dpy, src, &xf);
	XRenderSetPictureFilter(dpy, src, FilterGood, NULL, 0);

	XRenderComposite(dpy, sfmt->direct.alphaMask ? PictOpOver : PictOpSrc,
			 src, None, dst, 0, 0, 0, 0, x, y, w, h);

	XRenderFreePicture(dpy, src);
	XRenderFreePicture(dpy, dst);
	XFreePixmap(dpy, pixmap);
	return True;
#else
	(void) wwin; (void) dest; (void) x; (void) y; (void) w; (void) h;
	return False;
#endif
}

/* Snapshot of cache size scaled by the server, avoids reading the whole window */
static RImage *grabScaled(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	int cw = WMAX(1, (int) wwin->client.width), ch = WMAX(1, (int) wwin->client.height);
	double f = WMIN(1.0, WMIN((double) SNAPSHOT_MAX_WIDTH / cw, (double) SNAPSHOT_MAX_HEIGHT / ch));
	int w = WMAX(1, (int) (cw * f + 0.5)), h = WMAX(1, (int) (ch * f + 0.5));
	Pixmap pixmap;
	XImage *ximg;
	RImage *img = NULL;

	pixmap = XCreatePixmap(dpy, scr->root_win, w, h, scr->w_depth);
	if (wSnapshotRenderScaled(wwin, pixmap, 0, 0, w, h)) {
		ximg = XGetImage(dpy, pixmap, 0, 0, w, h, AllPlanes, ZPixmap);
		if (ximg) {
			img = imageFromXImage(scr->rcontext, ximg);
			XDestroyImage(ximg);
		}
	}
	XFreePixmap(dpy, pixmap);
	return img;
}

Bool wSnapshotCanGrab(WWindow *wwin, Window ignore)
{
	if (compositeActive())
		return wwin->frame && wwin->flags.mapped && !wwin->flags.shaded;
	return wSnapshotIsUnobscured(wwin, ignore);
}

RImage *wSnapshotGrabWindow(WWindow *wwin, Bool raise)
{
	WScreen *scr = wwin->screen_ptr;
	XWindowAttributes attr;
	XImage *pimg;
	RImage *img;
	Window child;
	int x, y, x0, y0, x1, y1;

	if (!wwin->client_win || !wwin->flags.mapped || wwin->flags.shaded)
		return NULL;

	if (!XGetWindowAttributes(dpy, wwin->client_win, &attr) || attr.map_state != IsViewable)
		return NULL;

#ifdef USE_COMPOSITE
	if (compositeActive()) {
		img = grabComposite(wwin);
		if (img)
			return img;
	}
#endif

	if (raise)
		XRaiseWindow(dpy, wwin->frame->core->window);

	if (!XTranslateCoordinates(dpy, wwin->client_win, scr->root_win, 0, 0, &x, &y, &child))
		return NULL;

	/* only the part of the window that is on screen can be read */
	x0 = WMAX(x, 0);
	y0 = WMAX(y, 0);
	x1 = WMIN(x + attr.width, scr->scr_width);
	y1 = WMIN(y + attr.height, scr->scr_height);
	if (x1 <= x0 || y1 <= y0)
		return NULL;

	pimg = XGetImage(dpy, wwin->client_win, x0 - x, y0 - y, x1 - x0, y1 - y0, AllPlanes, ZPixmap);
	if (!pimg)
		return NULL;

	img = imageFromXImage(scr->rcontext, pimg);
	XDestroyImage(pimg);

	return img;
}

RImage *wSnapshotScaleToFit(RImage *img, int max_w, int max_h)
{
	double fx, fy, f;
	int w, h;

	if (!img || max_w <= 0 || max_h <= 0)
		return NULL;

	if (img->width <= max_w && img->height <= max_h)
		return RCloneImage(img);

	fx = (double) max_w / img->width;
	fy = (double) max_h / img->height;
	f = WMIN(fx, fy);

	w = WMIN(WMAX(1, (int) (img->width * f + 0.5)), max_w);
	h = WMIN(WMAX(1, (int) (img->height * f + 0.5)), max_h);

	return boxScale(img, w, h);
}

RImage *wSnapshotScale(RImage *img, int w, int h)
{
	if (!img || w <= 0 || h <= 0)
		return NULL;
	if (img->width == w && img->height == h)
		return RCloneImage(img);
	if (w <= img->width && h <= img->height)
		return boxScale(img, w, h);
	return RSmoothScaleImage(img, w, h);
}

void wSnapshotStore(WWindow *wwin, RImage *full)
{
	RImage *scaled;

	scaled = wSnapshotScaleToFit(full, SNAPSHOT_MAX_WIDTH, SNAPSHOT_MAX_HEIGHT);
	if (!scaled)
		return;

	wSnapshotRelease(wwin);
	wwin->snapshot = scaled;
}

void wSnapshotUpdate(WWindow *wwin, Bool raise)
{
	RImage *full;

	if (wwin->frame->workspace != wwin->screen_ptr->current_workspace)
		return;

	if (!raise && compositeActive()) {
		RImage *img = grabScaled(wwin);

		if (img) {
			wSnapshotRelease(wwin);
			wwin->snapshot = img;
			return;
		}
	}

	full = wSnapshotGrabWindow(wwin, raise);
	if (full) {
		wSnapshotStore(wwin, full);
		RReleaseImage(full);
	}
}

void wSnapshotRelease(WWindow *wwin)
{
	if (wwin->snapshot) {
		RReleaseImage(wwin->snapshot);
		wwin->snapshot = NULL;
	}
	if (focus_capture.timer && focus_capture.client_win == wwin->client_win) {
		WMDeleteTimerHandler(focus_capture.timer);
		focus_capture.timer = NULL;
	}
}

Bool wSnapshotIsUnobscured(WWindow *wwin, Window ignore)
{
	WScreen *scr = wwin->screen_ptr;
	Window root, parent, *children = NULL, child;
	unsigned int nchildren, i;
	XWindowAttributes attr;
	int cx, cy, cw, ch;
	Bool above = False, clear = True;

	if (!wwin->flags.mapped || wwin->flags.shaded || !wwin->frame)
		return False;

	if (!XTranslateCoordinates(dpy, wwin->client_win, scr->root_win, 0, 0, &cx, &cy, &child))
		return False;
	cw = wwin->client.width;
	ch = wwin->client.height;

	if (!XQueryTree(dpy, scr->root_win, &root, &parent, &children, &nchildren))
		return False;

	/* children are listed from bottom to top of the stacking order */
	for (i = 0; i < nchildren && clear; i++) {
		if (!above) {
			above = (children[i] == wwin->frame->core->window);
			continue;
		}
		if (children[i] == ignore)
			continue;
		if (!XGetWindowAttributes(dpy, children[i], &attr))
			continue;
		if (attr.map_state != IsViewable || attr.class == InputOnly)
			continue;
		if (attr.x < cx + cw && attr.x + attr.width + 2 * attr.border_width > cx
		    && attr.y < cy + ch && attr.y + attr.height + 2 * attr.border_width > cy)
			clear = False;
	}

	if (children)
		XFree(children);

	return above && clear;
}

static void focusCaptureTimeout(void *data)
{
	WScreen *scr = focus_capture.scr;
	WWindow *wwin;

	(void) data;

	focus_capture.timer = NULL;

	wwin = scr->focused_window;
	if (!wwin || wwin->client_win != focus_capture.client_win)
		return;
	if (wwin->frame->workspace != scr->current_workspace)
		return;
	if (wSnapshotCanGrab(wwin, None))
		wSnapshotUpdate(wwin, False);
}

void wSnapshotScheduleFocused(WWindow *wwin)
{
	if (focus_capture.timer)
		WMDeleteTimerHandler(focus_capture.timer);

	focus_capture.scr = wwin->screen_ptr;
	focus_capture.client_win = wwin->client_win;
	focus_capture.timer = WMAddTimerHandler(FOCUS_SNAPSHOT_DELAY, focusCaptureTimeout, NULL);
}
