/* wmmonitors.c - WINGs monitor layout editor
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "WPrefs.h"
#include "PanelLayout.h"
#include "MonitorLayout.h"
#include <X11/Xatom.h>
#include <X11/Xlocale.h>
#include <ctype.h>

#ifdef USE_RANDR

typedef struct {
	WMScreen *screen;
	Display *dpy;
	Window root;
	WMWindow *window, *options, *confirmation;
	WMBox *column;
	WMBox *actions;
	WMFrame *canvas;
	WMLabel *status, *hintLabel, *confirmationLabel;
	WMButton *apply, *save, *load, *refresh, *keep, *revert;
	WMPopUpButton *rotation, *mode;
	WMButton *flipX, *flipY;
	MonitorLayout *live, *draft, *rollback;
	RImage **backgrounds;
	WMRect *rects;
	WMFont *font;
	WMColor *white, *black, *gray, *highlight, *gold, *starGray;
	WMHandlerID timer, refreshTimer;
	int seconds, selected, dragging, dragX, dragY, outputX, outputY, eventBase;
	int originX, originY, offsetX, offsetY;
	int pressedControl, controlOutput, trayPage, trayPages, trayY;
	double scale;
	Bool dirty;
	char *path;
} MonitorEditor;

static MonitorEditor *activeEditor;
static void paint(MonitorEditor *editor);
static void rebuild(MonitorEditor *editor);
static void reload(MonitorEditor *editor, const char *message);
static void revertChanges(WMWidget *widget, void *data);

static void report(MonitorEditor *editor, const char *error)
{
	wwarning("%s", error);
	WMSetLabelText(editor->status, error);
	WMRunAlertPanel(editor->screen, editor->window, _("Monitor Configuration"), error, _("OK"), NULL, NULL);
}

static WMPropList *dictValue(WMPropList *dict, const char *name)
{
	WMPropList *key = WMCreatePLString(name), *value = NULL;

	if (dict && WMIsPLDictionary(dict))
		value = WMGetFromPLDictionary(dict, key);
	WMReleasePropList(key);
	return value;
}

static const char *arrayString(WMPropList *array, int index)
{
	WMPropList *value;

	if (!array || !WMIsPLArray(array) || index >= WMGetPropListItemCount(array))
		return NULL;
	value = WMGetFromPLArray(array, index);
	return WMIsPLString(value) ? WMGetFromPLString(value) : NULL;
}

static RColor textureColor(MonitorEditor *editor, const char *text)
{
	XColor color;
	RColor result = { 0x50, 0x50, 0x75, 0xff };

	if (text && XParseColor(editor->dpy, DefaultColormap(editor->dpy, DefaultScreen(editor->dpy)), text, &color)) {
		result.red = color.red >> 8;
		result.green = color.green >> 8;
		result.blue = color.blue >> 8;
	}
	return result;
}

/* Disabled/new outputs have no root-pixmap crop: preview their saved texture instead. */
static RImage *defaultBackground(MonitorEditor *editor, const MonitorOutput *output, WMPropList *domain)
{
	WMPropList *texture = dictValue(dictValue(domain, "MonitorSpecificBack"), output->name);
	const char *type, *file;
	Bool pixmap;
	int placement;
	RColor color;
	RImage *canvas, *image, *scaled;
	int w = 480, h = 300, mw, mh;

	if (!texture)
		texture = dictValue(domain, "WorkspaceBack");
	type = arrayString(texture, 0);
	pixmap = type && strlen(type) == 7 && !strcasecmp(type + 1, "pixmap");
	placement = type ? tolower((unsigned char)type[0]) : 0;
	MonitorOutputSize(output, &mw, &mh);
	if (mw > 0 && mh > 0)
		h = WMAX(1, (int)((long long)w * mh / mw));
	h = WMIN(h, 960);
	color = textureColor(editor, arrayString(texture, pixmap ? 2 : 1));
	if (type && strlen(type) == 9 && !strcasecmp(type + 1, "gradient") &&
	    (placement == 'h' || placement == 'v' || placement == 'd')) {
		RColor end = textureColor(editor, arrayString(texture, 2));

		return RRenderGradient(w, h, &color, &end,
				       placement == 'h' ? RHorizontalGradient :
				       placement == 'v' ? RVerticalGradient : RDiagonalGradient);
	}
	canvas = RCreateImage(w, h, False);
	if (!canvas)
		return NULL;
	RClearImage(canvas, &color);
	if (!pixmap) {
		if (type && strcasecmp(type, "solid"))
			wwarning(_("Background texture %s cannot be previewed for %s; using the default color."), type, output->name);
		return canvas;
	}
	file = arrayString(texture, 1);
	if (file) {
		WMPropList *paths = dictValue(domain, "PixmapPath");
		char **search = NULL, *path;
		int i, count = paths && WMIsPLArray(paths) ? WMGetPropListItemCount(paths) : 0;

		search = wmalloc(sizeof(*search) * (count + 1));
		for (i = 0; i < count; i++) {
			WMPropList *item = WMGetFromPLArray(paths, i);

			search[i] = WMIsPLString(item) ? WMGetFromPLString(item) : "";
		}
		path = wfindfileinlist(search, file);
		wfree(search);
		image = path ? RLoadImage(WMScreenRContext(editor->screen), path, 0) : NULL;
		if (path)
			wfree(path);
	} else {
		image = NULL;
	}
	if (!image) {
		wwarning(_("Could not load the saved background preview for %s."), output->name);
		return canvas;
	}
	if (placement == 't') {
		int x, y, tw = mw > 0 ? WMAX(1, (int)((long long)image->width * w / mw)) : image->width;
		int th = mh > 0 ? WMAX(1, (int)((long long)image->height * h / mh)) : image->height;

		scaled = RScaleImage(image, WMIN(tw, 1920), WMIN(th, 1920));
		if (scaled)
			for (y = 0; y < h; y += scaled->height)
				for (x = 0; x < w; x += scaled->width)
					RCopyArea(canvas, scaled, 0, 0, WMIN(scaled->width, w - x),
						  WMIN(scaled->height, h - y), x, y);
	} else {
		int sw = w, sh = h;

		if (placement == 'f' || placement == 'm') {
			if ((image->width * (long long)h > image->height * (long long)w) == (placement == 'm')) {
				sw = w;
				sh = WMAX(1, (int)((long long)w * image->height / image->width));
			} else {
				sh = h;
				sw = WMAX(1, (int)((long long)h * image->width / image->height));
			}
		} else if (placement == 'c') {
			sw = mw > 0 ? WMAX(1, (int)((long long)image->width * w / mw)) : image->width;
			sh = mh > 0 ? WMAX(1, (int)((long long)image->height * h / mh)) : image->height;
		}
		scaled = RScaleImage(image, WMIN(sw, 1920), WMIN(sh, 1920));
		if (scaled) {
			int cw = WMIN(w, scaled->width), ch = WMIN(h, scaled->height);

			RCopyArea(canvas, scaled, (scaled->width - cw) / 2, (scaled->height - ch) / 2,
				  cw, ch, (w - cw) / 2, (h - ch) / 2);
		}
	}
	if (scaled)
		RReleaseImage(scaled);
	RReleaseImage(image);
	return canvas;
}

static int previewError;

static int backgroundError(Display *dpy, XErrorEvent *event)
{
	(void)dpy;
	previewError = event->error_code;
	return 0;
}

static void grabBackgrounds(MonitorEditor *editor)
{
	Atom prop = XInternAtom(editor->dpy, "_XROOTPMAP_ID", True), type;
	unsigned char *data = NULL;
	unsigned long count, after;
	int format, i, (*handler)(Display *, XErrorEvent *);
	RImage *desktop = NULL;
	char *path = wdefaultspathfordomain("WindowMaker");
	WMPropList *domain = WMReadPropListFromFile(path);

	wfree(path);
	if (prop != None && XGetWindowProperty(editor->dpy, editor->root, prop, 0, 1, False, XA_PIXMAP,
					      &type, &format, &count, &after, &data) == Success &&
	    data && type == XA_PIXMAP && format == 32 && count == 1) {
		Pixmap pixmap = *(Pixmap *)data;
		Window dummy;
		int x, y;
		unsigned int width, height, border, depth;

		XSync(editor->dpy, False);
		previewError = 0;
		handler = XSetErrorHandler(backgroundError);
		if (XGetGeometry(editor->dpy, pixmap, &dummy, &x, &y, &width, &height, &border, &depth) &&
		    (unsigned long long)width * height <= 32000000)
			desktop = RCreateImageFromDrawable(WMScreenRContext(editor->screen), pixmap, None);
		XSync(editor->dpy, False);
		XSetErrorHandler(handler);
		if (previewError)
			wwarning(_("The desktop background pixmap is no longer available (X error %d)."), previewError);
	}
	if (data)
		XFree(data);
	editor->backgrounds = wmalloc(sizeof(*editor->backgrounds) * WMAX(1, editor->live->count));
	for (i = 0; i < editor->live->count; i++) {
		MonitorOutput *o = &editor->live->outputs[i];
		int w, h;
		RImage *crop = NULL;

		MonitorOutputSize(o, &w, &h);
		if (desktop && o->enabled && o->x >= 0 && o->y >= 0 && w > 0 && h > 0 &&
		    o->x + w <= desktop->width && o->y + h <= desktop->height)
			crop = RGetSubImage(desktop, o->x, o->y, w, h);
		if (crop) {
			editor->backgrounds[i] = RScaleImage(crop, 480, WMIN(960, WMAX(1, (int)((long long)480 * h / w))));
			RReleaseImage(crop);
		} else {
			editor->backgrounds[i] = defaultBackground(editor, o, domain);
		}
	}
	if (desktop)
		RReleaseImage(desktop);
	if (domain)
		WMReleasePropList(domain);
}

static void freeBackgrounds(MonitorEditor *editor)
{
	int i;

	if (!editor->backgrounds)
		return;
	for (i = 0; i < editor->live->count; i++)
		if (editor->backgrounds[i])
			RReleaseImage(editor->backgrounds[i]);
	wfree(editor->backgrounds);
	editor->backgrounds = NULL;
}

static RROutput effectivePrimary(MonitorEditor *editor)
{
	int i;
	RROutput first = None;

	for (i = 0; i < editor->draft->count; i++) {
		MonitorOutput *o = &editor->draft->outputs[i];

		if (!o->enabled)
			continue;
		if (o->id == editor->draft->primary)
			return o->id;
		if (first == None)
			first = o->id;
	}
	return first;
}

/* Mirrored cards expose their individual control strips instead of hiding them. */
static int mirrorOffset(MonitorEditor *editor, int index)
{
	MonitorOutput *o = &editor->draft->outputs[index];
	int i, w, h, count = 0;

	MonitorOutputSize(o, &w, &h);
	for (i = 0; i < index; i++) {
		MonitorOutput *p = &editor->draft->outputs[i];
		int pw, ph;

		MonitorOutputSize(p, &pw, &ph);
		if (p->enabled && p->x == o->x && p->y == o->y && pw == w && ph == h)
			count++;
	}
	return count * (WMAX(24, (int)WMFontHeight(editor->font) + 8) + 8);
}

static void fitCanvas(MonitorEditor *editor)
{
	WMSize size = WMGetViewSize(WMWidgetView(editor->canvas));
	int i, minX = 0, minY = 0, maxX = 1, maxY = 1, disabled = 0;
	int fontHeight = WMFontHeight(editor->font), side = WMAX(24, fontHeight + 8);
	int cardWidth = WMAX(160, 8 * fontHeight), cardHeight = 4 * fontHeight + side + 20;
	int columns = WMAX(1, ((int)size.width - 24) / (cardWidth + 8));
	int activeHeight, stagger = 0;

	for (i = 0; i < editor->draft->count; i++) {
		MonitorOutput *o = &editor->draft->outputs[i];
		int w, h;

		if (!o->connected)
			continue;
		if (!o->enabled) {
			disabled++;
			continue;
		}
		MonitorOutputSize(o, &w, &h);
		stagger = WMAX(stagger, mirrorOffset(editor, i));
		minX = WMIN(minX, o->x);
		minY = WMIN(minY, o->y);
		maxX = WMAX(maxX, o->x + w);
		maxY = WMAX(maxY, o->y + h);
	}
	editor->trayPages = (disabled + columns - 1) / columns;
	editor->trayPage = WMAX(0, WMIN(editor->trayPage, editor->trayPages - 1));
	editor->trayY = disabled ? (int)size.height - cardHeight - fontHeight - 28 : (int)size.height;
	activeHeight = WMAX(1, editor->trayY);
	editor->scale = WMIN((double)WMAX(1, (int)size.width - 100) / (maxX - minX),
			    (double)WMAX(1, activeHeight - 100 - stagger) / (maxY - minY));
	editor->originX = minX;
	editor->originY = minY;
	editor->offsetX = ((int)size.width - (maxX - minX) * editor->scale) / 2;
	editor->offsetY = (activeHeight - (maxY - minY) * editor->scale + stagger) / 2;
}

static int controlSize(MonitorEditor *editor)
{
	return WMAX(24, (int)WMFontHeight(editor->font) + 8);
}

static WMRect monitorControlRect(MonitorEditor *editor, int index, Bool primary)
{
	WMRect r = editor->rects[index];
	int side = controlSize(editor);

	return wmkrect(primary ? r.pos.x + 4 : r.pos.x + (int)r.size.width - side - 4,
		       r.pos.y + (int)r.size.height - side - 4, side, side);
}

static Bool containsPoint(WMRect rect, int x, int y)
{
	return rect.size.width && rect.size.height && x >= rect.pos.x && y >= rect.pos.y &&
	       x < rect.pos.x + (int)rect.size.width && y < rect.pos.y + (int)rect.size.height;
}

static WMRect trayArrowRect(MonitorEditor *editor, Bool next)
{
	int side = controlSize(editor);
	int width = WMGetViewSize(WMWidgetView(editor->canvas)).width;

	return wmkrect(width - (next ? 1 : 2) * (side + 4) - 12, editor->trayY, side, side);
}

static void drawStar(MonitorEditor *editor, Drawable drawable, WMRect rect, Bool primary, Bool sensitive)
{
	const int points[][2] = {
		{50, 0}, {61, 35}, {98, 35}, {68, 57}, {79, 93},
		{50, 71}, {21, 93}, {32, 57}, {2, 35}, {39, 35}
	};
	XPoint star[11];
	int i, side = (int)rect.size.width - 4;

	for (i = 0; i < wlengthof(points); i++) {
		star[i].x = rect.pos.x + 2 + points[i][0] * side / 100;
		star[i].y = rect.pos.y + 2 + points[i][1] * side / 100;
	}
	star[10] = star[0];
	XFillPolygon(editor->dpy, drawable, WMColorGC(primary ? editor->gold : editor->starGray),
		     star, 10, Nonconvex, CoordModeOrigin);
	XDrawLines(editor->dpy, drawable, WMColorGC(sensitive ? editor->white : editor->gray),
		   star, 11, CoordModeOrigin);
}

static void drawCheckbox(MonitorEditor *editor, Drawable drawable, WMRect rect, Bool enabled, Bool sensitive)
{
	int side = (int)rect.size.width - 8, x = rect.pos.x + 4, y = rect.pos.y + 4;
	XPoint check[] = {
		{ x + side / 5, y + side / 2 },
		{ x + 2 * side / 5, y + 3 * side / 4 },
		{ x + 4 * side / 5, y + side / 4 }
	};

	XFillRectangle(editor->dpy, drawable, WMColorGC(sensitive ? editor->white : editor->starGray),
		       x, y, side, side);
	XDrawRectangle(editor->dpy, drawable, WMColorGC(editor->gray), x, y, side, side);
	if (enabled)
		XDrawLines(editor->dpy, drawable, WMColorGC(editor->black), check, 3, CoordModeOrigin);
}

static void paint(MonitorEditor *editor)
{
	WMSize size = WMGetViewSize(WMWidgetView(editor->canvas));
	Window window = WMWidgetXID(editor->canvas);
	WMPixmap *buffer;
	Drawable drawable;
	int i, disabled = 0;
	int textHeight = WMFontHeight(editor->font), side = controlSize(editor);
	int cardWidth = WMAX(160, 8 * textHeight), cardHeight = 4 * textHeight + side + 20;
	int columns = WMAX(1, ((int)size.width - 24) / (cardWidth + 8));

	if (!window || !size.width || !size.height)
		return;
	if (editor->dragging < 0)
		fitCanvas(editor);
	buffer = WMCreatePixmap(editor->screen, size.width, size.height, WMScreenDepth(editor->screen), False);
	if (!buffer)
		return;
	drawable = WMGetPixmapXID(buffer);
	XFillRectangle(editor->dpy, drawable, WMColorGC(editor->gray), 0, 0, size.width, size.height);
	if (editor->trayPages) {
		const char *text = editor->trayPages > 1 ? _("Disabled monitors (scroll to browse)") : _("Disabled monitors");

		XDrawLine(editor->dpy, drawable, WMColorGC(editor->starGray), 12, editor->trayY - 8,
			  (int)size.width - 12, editor->trayY - 8);
		WMDrawString(editor->screen, drawable, editor->white, editor->font, 16, editor->trayY,
			     text, strlen(text));
		if (editor->trayPages > 1) {
			int next;

			for (next = 0; next < 2; next++) {
				WMRect r = trayArrowRect(editor, next);
				XPoint arrow[] = {
					{ r.pos.x + (next ? side / 3 : 2 * side / 3), r.pos.y + side / 4 },
					{ r.pos.x + (next ? 2 * side / 3 : side / 3), r.pos.y + side / 2 },
					{ r.pos.x + (next ? side / 3 : 2 * side / 3), r.pos.y + 3 * side / 4 }
				};

				XFillPolygon(editor->dpy, drawable, WMColorGC(editor->white), arrow, 3,
					     Convex, CoordModeOrigin);
			}
		}
	}
	for (i = 0; i < editor->draft->count; i++) {
		MonitorOutput *o = &editor->draft->outputs[i];
		WMRect *r = &editor->rects[i];
		RImage *image = NULL;
		WMPixmap *preview;
		char text[256];
		int w, h;

		*r = wmkrect(0, 0, 0, 0);
		if (!o->connected)
			continue;
		MonitorOutputSize(o, &w, &h);
		if (o->enabled) {
			*r = wmkrect(editor->offsetX + (o->x - editor->originX) * editor->scale,
				    editor->offsetY + (o->y - editor->originY) * editor->scale - mirrorOffset(editor, i),
				    WMAX(2 * side + 12, (int)(w * editor->scale)),
				    WMAX(2 * textHeight + side + 20, (int)(h * editor->scale)));
		} else {
			int slot = disabled++ - editor->trayPage * columns;

			if (slot < 0 || slot >= columns)
				continue;
			*r = wmkrect(16 + slot * (cardWidth + 8), (int)size.height - cardHeight - 12,
				    WMIN(cardWidth, (int)size.width - 32), cardHeight);
		}
		if (editor->backgrounds[i])
			image = RScaleImage(editor->backgrounds[i], r->size.width, r->size.height);
		if (image && !o->enabled) {
			RColor gray = { 0x70, 0x70, 0x70, 0xff };
			RImage *dimmed = RCreateImage(image->width, image->height, False);

			if (dimmed) {
				RClearImage(dimmed, &gray);
				RCombineImagesWithOpaqueness(dimmed, image, 96);
				RReleaseImage(image);
				image = dimmed;
			}
		}
		preview = image ? WMCreatePixmapFromRImage(editor->screen, image, 128) : NULL;
		if (preview) {
			WMDrawPixmap(preview, drawable, r->pos.x, r->pos.y);
			WMReleasePixmap(preview);
		}
		if (image)
			RReleaseImage(image);
		XFillRectangle(editor->dpy, drawable, WMColorGC(editor->black), r->pos.x, r->pos.y,
			       r->size.width, WMIN(r->size.height, (unsigned)(2 * textHeight + 8)));
		snprintf(text, sizeof(text), "%s", o->name);
		WMDrawString(editor->screen, drawable, editor->white, editor->font, r->pos.x + 5, r->pos.y + 2,
			     text, strlen(text));
		if (o->enabled)
			snprintf(text, sizeof(text), "%dx%d  +%d+%d", w, h, o->x, o->y);
		else
			snprintf(text, sizeof(text), "%s", o->nmode ? _("Disabled") : _("No usable modes"));
		WMDrawString(editor->screen, drawable, editor->white, editor->font, r->pos.x + 5,
			     r->pos.y + textHeight + 3, text, strlen(text));
		XFillRectangle(editor->dpy, drawable, WMColorGC(editor->black), r->pos.x,
			       r->pos.y + (int)r->size.height - side - 8, r->size.width, side + 8);
		if (WMWidthOfString(editor->font, o->name, strlen(o->name)) + 2 * side + 16 < (int)r->size.width)
			WMDrawString(editor->screen, drawable, editor->white, editor->font,
				     r->pos.x + side + 8, r->pos.y + (int)r->size.height - side - 4,
				     o->name, strlen(o->name));
		drawStar(editor, drawable, monitorControlRect(editor, i, True),
			 o->enabled && o->id == effectivePrimary(editor), o->enabled && !editor->rollback);
		drawCheckbox(editor, drawable, monitorControlRect(editor, i, False),
			     o->enabled, o->nmode > 0 && !editor->rollback);
		XDrawRectangle(editor->dpy, drawable,
			       WMColorGC(i == editor->selected ? editor->highlight : editor->white),
			       r->pos.x, r->pos.y, r->size.width - 1, r->size.height - 1);
	}
	WMDrawPixmap(buffer, window, 0, 0);
	WMReleasePixmap(buffer);
}

static void updateButtons(MonitorEditor *editor)
{
	Bool pending = editor->rollback != NULL;

	WMSetButtonEnabled(editor->apply, !pending && editor->dirty);
	WMSetButtonEnabled(editor->save, !pending && !editor->dirty);
	WMSetButtonEnabled(editor->load, !pending);
	WMSetButtonEnabled(editor->refresh, !pending);
	WMSetButtonEnabled(editor->keep, pending);
	WMSetButtonEnabled(editor->revert, pending);
}

static void edited(MonitorEditor *editor)
{
	editor->dirty = True;
	WMSetLabelText(editor->status, _("Changes are staged. Apply to test them; Save Layout stores the confirmed layout."));
	updateButtons(editor);
	paint(editor);
}

static void primaryChanged(MonitorEditor *editor, int index)
{
	if (!editor->draft->outputs[index].enabled || editor->draft->primary == editor->draft->outputs[index].id)
		return;
	editor->draft->primary = editor->draft->outputs[index].id;
	edited(editor);
}

static void enabledChanged(MonitorEditor *editor, int index)
{
	int i, enabled = 0, right = 0;

	if (editor->draft->outputs[index].nmode == 0)
		return;

	for (i = 0; i < editor->draft->count; i++) {
		MonitorOutput *o = &editor->draft->outputs[i];
		int w, h;

		if (o->enabled) {
			enabled++;
			MonitorOutputSize(o, &w, &h);
			right = WMAX(right, o->x + w);
		}
	}
	if (editor->draft->outputs[index].enabled && enabled == 1) {
		report(editor, _("At least one monitor must remain enabled."));
		return;
	}
	editor->draft->outputs[index].enabled = !editor->draft->outputs[index].enabled;
	WMUnmapWidget(editor->options);
	if (editor->draft->outputs[index].enabled) {
		editor->draft->outputs[index].x = right;
		editor->draft->outputs[index].y = 0;
		if (editor->draft->primary == None)
			editor->draft->primary = editor->draft->outputs[index].id;
	} else if (editor->draft->primary == editor->draft->outputs[index].id) {
		for (i = 0; i < editor->draft->count; i++)
			if (editor->draft->outputs[i].enabled) {
				editor->draft->primary = editor->draft->outputs[i].id;
				break;
			}
	}
	MonitorLayoutNormalize(editor->draft);
	edited(editor);
}

static void optionChanged(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;
	MonitorOutput *o;
	int index;

	(void)widget;
	if (editor->selected < 0 || editor->rollback)
		return;
	o = &editor->draft->outputs[editor->selected];
	index = WMGetPopUpButtonSelectedItem(editor->rotation);
	o->rotation = (1 << index) | (WMGetButtonSelected(editor->flipX) ? RR_Reflect_X : 0) |
		      (WMGetButtonSelected(editor->flipY) ? RR_Reflect_Y : 0);
	index = WMGetPopUpButtonSelectedItem(editor->mode);
	if (index >= 0 && index < o->nmode)
		o->mode = index;
	edited(editor);
}

static void closeOptions(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;

	(void)widget;
	WMUnmapWidget(editor->options);
}

static void showOptions(MonitorEditor *editor, int x, int y)
{
	MonitorOutput *o = &editor->draft->outputs[editor->selected];
	int i;
	char text[256];

	WMSetWindowTitle(editor->options, o->name);
	for (i = WMGetPopUpButtonNumberOfItems(editor->mode) - 1; i >= 0; i--)
		WMRemovePopUpButtonItem(editor->mode, i);
	for (i = 0; i < o->nmode; i++) {
		MonitorMode *mode = &o->modes[i];

		snprintf(text, sizeof(text), "%s (%.2f Hz)", mode->name, mode->rate / 1000.0);
		WMAddPopUpButtonItem(editor->mode, text);
	}
	WMSetPopUpButtonSelectedItem(editor->mode, o->mode);
	for (i = 0; i < 4; i++) {
		WMSetPopUpButtonItemEnabled(editor->rotation, i, !!(o->supported & (1 << i)));
		if (o->rotation & (1 << i))
			WMSetPopUpButtonSelectedItem(editor->rotation, i);
	}
	WMSetButtonSelected(editor->flipX, !!(o->rotation & RR_Reflect_X));
	WMSetButtonSelected(editor->flipY, !!(o->rotation & RR_Reflect_Y));
	WMSetButtonEnabled(editor->flipX, !!(o->supported & RR_Reflect_X));
	WMSetButtonEnabled(editor->flipY, !!(o->supported & RR_Reflect_Y));
	{
		WMSize size = WMGetViewSize(WMWidgetView(editor->options));

		WMMoveWidget(editor->options, WMAX(0, WMIN(x, (int)WMScreenWidth(editor->screen) - (int)size.width)),
			     WMAX(0, WMIN(y, (int)WMScreenHeight(editor->screen) - (int)size.height)));
	}
	WMMapWidget(editor->options);
}

static void snapOutput(MonitorEditor *editor, int index)
{
	MonitorOutput *o = &editor->draft->outputs[index];
	int i, w, h, threshold = WMAX(1, (int)(12 / editor->scale));
	int dx = threshold + 1, dy = threshold + 1;

	MonitorOutputSize(o, &w, &h);
	for (i = 0; i < editor->draft->count; i++) {
		MonitorOutput *p = &editor->draft->outputs[i];
		int pw, ph, j;
		int xs[4], ys[4];

		if (i == index || !p->enabled)
			continue;
		MonitorOutputSize(p, &pw, &ph);
		xs[0] = p->x; xs[1] = p->x + pw; xs[2] = p->x - w; xs[3] = p->x + pw - w;
		ys[0] = p->y; ys[1] = p->y + ph; ys[2] = p->y - h; ys[3] = p->y + ph - h;
		for (j = 0; j < 4; j++) {
			if (abs(xs[j] - o->x) < abs(dx))
				dx = xs[j] - o->x;
			if (abs(ys[j] - o->y) < abs(dy))
				dy = ys[j] - o->y;
		}
	}
	if (abs(dx) <= threshold)
		o->x += dx;
	if (abs(dy) <= threshold)
		o->y += dy;
}

static void canvasEvent(XEvent *event, void *data)
{
	MonitorEditor *editor = data;
	int i;

	if (event->type == Expose || event->type == ConfigureNotify) {
		paint(editor);
		return;
	}
	if (editor->rollback)
		return;
	if (event->type == ButtonPress) {
		editor->pressedControl = 0;
		if (editor->trayPages > 1 &&
		    (event->xbutton.button == Button4 || event->xbutton.button == Button5 ||
		     (event->xbutton.button == Button1 &&
		      (containsPoint(trayArrowRect(editor, False), event->xbutton.x, event->xbutton.y) ||
		       containsPoint(trayArrowRect(editor, True), event->xbutton.x, event->xbutton.y))))) {
			Bool next = event->xbutton.button == Button5 ||
				    (event->xbutton.button == Button1 &&
				     containsPoint(trayArrowRect(editor, True), event->xbutton.x, event->xbutton.y));

			editor->trayPage = (editor->trayPage + (next ? 1 : editor->trayPages - 1)) % editor->trayPages;
			paint(editor);
			return;
		}
		for (i = editor->draft->count - 1; i >= 0; i--) {
			WMRect *r = &editor->rects[i];

			if (!containsPoint(*r, event->xbutton.x, event->xbutton.y))
				continue;
			editor->selected = i;
			if (event->xbutton.button == Button3) {
				showOptions(editor, event->xbutton.x_root, event->xbutton.y_root);
			} else if (event->xbutton.button == Button1) {
				WMUnmapWidget(editor->options);
				if (containsPoint(monitorControlRect(editor, i, True), event->xbutton.x, event->xbutton.y))
					editor->pressedControl = 1;
				else if (containsPoint(monitorControlRect(editor, i, False), event->xbutton.x, event->xbutton.y))
					editor->pressedControl = 2;
				else if (editor->draft->outputs[i].enabled) {
					editor->dragging = i;
					editor->dragX = event->xbutton.x;
					editor->dragY = event->xbutton.y;
					editor->outputX = editor->draft->outputs[i].x;
					editor->outputY = editor->draft->outputs[i].y;
				}
				editor->controlOutput = i;
			}
			paint(editor);
			break;
		}
	} else if (event->type == MotionNotify && editor->dragging >= 0) {
		MonitorOutput *o = &editor->draft->outputs[editor->dragging];

		o->x = editor->outputX + (event->xmotion.x - editor->dragX) / editor->scale;
		o->y = editor->outputY + (event->xmotion.y - editor->dragY) / editor->scale;
		paint(editor);
	} else if (event->type == ButtonRelease && event->xbutton.button == Button1) {
		int control = editor->pressedControl;

		editor->pressedControl = 0;
		if (control && containsPoint(monitorControlRect(editor, editor->controlOutput, control == 1),
					     event->xbutton.x, event->xbutton.y)) {
			if (control == 1)
				primaryChanged(editor, editor->controlOutput);
			else
				enabledChanged(editor, editor->controlOutput);
		} else if (editor->dragging >= 0) {
			snapOutput(editor, editor->dragging);
			editor->dragging = -1;
			MonitorLayoutNormalize(editor->draft);
			edited(editor);
		}
	}
}

static void rebuild(MonitorEditor *editor)
{
	wfree(editor->rects);
	editor->rects = wmalloc(sizeof(*editor->rects) * WMAX(1, editor->draft->count));
	editor->pressedControl = 0;
	editor->trayPage = 0;
	if (WMWidgetXID(editor->window))
		WMRealizeWidget(editor->column);
	updateButtons(editor);
	paint(editor);
}

static void reload(MonitorEditor *editor, const char *message)
{
	char error[512];
	MonitorLayout *live = MonitorLayoutQuery(editor->dpy, editor->root, error, sizeof(error));

	if (!live) {
		report(editor, error);
		return;
	}
	freeBackgrounds(editor);
	MonitorLayoutFree(editor->live);
	MonitorLayoutFree(editor->draft);
	editor->live = live;
	editor->draft = MonitorLayoutCopy(live);
	editor->dirty = False;
	editor->selected = editor->dragging = -1;
	WMUnmapWidget(editor->options);
	grabBackgrounds(editor);
	rebuild(editor);
	WMSetLabelText(editor->status, message);
}

static void refreshClicked(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;

	(void)widget;
	if (editor->dirty && WMRunAlertPanel(editor->screen, editor->window, _("Discard changes?"),
					    _("Reload the live layout and discard your staged changes?"),
					    _("Reload"), _("Cancel"), NULL) != WAPRDefault)
		return;
	reload(editor, _("Live monitor configuration loaded."));
}

static void keepChanges(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;

	(void)widget;
	if (editor->timer)
		WMDeleteTimerHandler(editor->timer);
	editor->timer = NULL;
	MonitorLayoutFree(editor->rollback);
	editor->rollback = NULL;
	WMUnmapWidget(editor->confirmation);
	reload(editor, _("Layout confirmed for this session. Use Save Layout to store it for later."));
}

static void revertChanges(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;
	char error[512];

	(void)widget;
	if (!editor->rollback)
		return;
	if (editor->timer)
		WMDeleteTimerHandler(editor->timer);
	editor->timer = NULL;
	{
		Bool restored = MonitorLayoutRestore(editor->dpy, editor->root, editor->rollback, error, sizeof(error));

		if (!restored)
			report(editor, error);
		MonitorLayoutFree(editor->rollback);
		editor->rollback = NULL;
		WMUnmapWidget(editor->confirmation);
		reload(editor, restored ? _("Previous layout restored.") : _("Could not restore the previous layout. Review the current configuration."));
	}
}

static void confirmationTick(void *data)
{
	MonitorEditor *editor = data;
	char text[256];

	editor->timer = NULL;
	if (--editor->seconds <= 0) {
		revertChanges(NULL, editor);
		return;
	}
	snprintf(text, sizeof(text), _("Keep this layout? Reverting in %d seconds."), editor->seconds);
	WMSetLabelText(editor->status, text);
	WMSetLabelText(editor->confirmationLabel, text);
	editor->timer = WMAddTimerHandler(1000, confirmationTick, editor);
}

static void applyChanges(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;
	MonitorLayout *current;
	char error[512], text[256];

	(void)widget;
	MonitorLayoutNormalize(editor->draft);
	current = MonitorLayoutQuery(editor->dpy, editor->root, error, sizeof(error));
	if (!current) {
		report(editor, error);
		return;
	}
	if (!MonitorLayoutSame(current, editor->live, False)) {
		MonitorLayoutFree(current);
		reload(editor, _("The live configuration changed. Staged changes were discarded; edit the new layout."));
		return;
	}
	if (!MonitorLayoutApply(editor->dpy, editor->root, editor->draft, error, sizeof(error))) {
		MonitorLayoutFree(current);
		report(editor, error);
		return;
	}
	editor->rollback = current;
	editor->seconds = 15;
	WMUnmapWidget(editor->options);
	updateButtons(editor);
	paint(editor);
	snprintf(text, sizeof(text), _("Keep this layout? Reverting in %d seconds."), editor->seconds);
	WMSetLabelText(editor->status, text);
	WMSetLabelText(editor->confirmationLabel, text);
	{
		int i;

		for (i = 0; i < editor->draft->count; i++)
			if (editor->draft->outputs[i].enabled &&
			    (editor->draft->primary == None || editor->draft->outputs[i].id == editor->draft->primary)) {
				WMMoveWidget(editor->confirmation, editor->draft->outputs[i].x + 10,
					     editor->draft->outputs[i].y + 10);
				break;
			}
	}
	WMMapWidget(editor->confirmation);
	editor->timer = WMAddTimerHandler(1000, confirmationTick, editor);
}

static void saveLayout(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;
	char error[512], text[1024];
	MonitorLayout *current = MonitorLayoutQuery(editor->dpy, editor->root, error, sizeof(error));

	(void)widget;
	if (!current) {
		report(editor, error);
		return;
	}
	if (!MonitorLayoutSame(current, editor->live, False)) {
		MonitorLayoutFree(current);
		reload(editor, _("The live configuration changed. Review it before saving."));
		return;
	}
	MonitorLayoutFree(current);
	if (!MonitorLayoutSave(editor->live, editor->path, error, sizeof(error))) {
		report(editor, error);
		return;
	}
	snprintf(text, sizeof(text), _("Layout saved to %s. Load Layout stages it; Apply activates it."), editor->path);
	WMSetLabelText(editor->status, text);
}

static void loadLayout(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;
	char error[512];

	(void)widget;
	if (editor->dirty && WMRunAlertPanel(editor->screen, editor->window, _("Discard changes?"),
					    _("Replace your staged changes with the saved layout?"),
					    _("Load Layout"), _("Cancel"), NULL) != WAPRDefault)
		return;
	if (!MonitorLayoutLoad(editor->draft, editor->path, error, sizeof(error))) {
		report(editor, error);
		return;
	}
	edited(editor);
	WMSetLabelText(editor->status, _("Saved layout staged. Missing monitors are ignored; new monitors keep their settings."));
}

static void monitorChanged(void *data)
{
	MonitorEditor *editor = data;
	MonitorLayout *current;
	char error[512];

	editor->refreshTimer = NULL;
	current = MonitorLayoutQuery(editor->dpy, editor->root, error, sizeof(error));
	if (!current) {
		wwarning("%s", error);
		WMSetLabelText(editor->status, error);
		return;
	}
	if (editor->rollback) {
		if (!MonitorLayoutSame(current, editor->rollback, True)) {
			/* A disconnected output cannot be restored safely from the old snapshot. */
			WMDeleteTimerHandler(editor->timer);
			editor->timer = NULL;
			MonitorLayoutFree(editor->rollback);
			editor->rollback = NULL;
			WMUnmapWidget(editor->confirmation);
			reload(editor, _("Monitors changed during confirmation. Review the current layout; the old layout cannot be restored."));
		}
	} else if (!MonitorLayoutSame(current, editor->live, False)) {
		reload(editor, _("Monitors changed. The live layout was reloaded and staged changes were discarded."));
	}
	MonitorLayoutFree(current);
}

static void eventHook(XEvent *event)
{
	MonitorEditor *editor = activeEditor;

	if (event->type != editor->eventBase + RRScreenChangeNotify && event->type != editor->eventBase + RRNotify)
		return;
	XRRUpdateConfiguration(event);
	if (editor->refreshTimer)
		WMDeleteTimerHandler(editor->refreshTimer);
	editor->refreshTimer = WMAddTimerHandler(200, monitorChanged, editor);
}

static void closeEditor(WMWidget *widget, void *data)
{
	MonitorEditor *editor = data;

	(void)widget;
	if (editor->rollback)
		revertChanges(NULL, editor);
	else if (editor->dirty && WMRunAlertPanel(editor->screen, editor->window, _("Discard changes?"),
						 _("Close without applying your staged changes?"),
						 _("Close"), _("Cancel"), NULL) != WAPRDefault)
		return;
	exit(0);
}

static WMButton *actionButton(WMBox *box, const char *text, WMAction *action, MonitorEditor *editor)
{
	WMButton *button = WMCreateCommandButton(box);

	WMSetButtonText(button, text);
	WMSetButtonAction(button, action, editor);
	WPrefsPackButton(box, button, 8);
	return button;
}

static void createEditor(MonitorEditor *editor)
{
	WMBox *actions, *options, *confirmation, *confirmationActions;
	WMLabel *label;
	WMSize size;
	const char *rotations[] = { _("Normal"), _("Rotate left"), _("Upside down"), _("Rotate right") };
	int i;

	editor->font = WMDefaultSystemFont(editor->screen);
	editor->white = WMWhiteColor(editor->screen);
	editor->black = WMBlackColor(editor->screen);
	editor->gray = WMDarkGrayColor(editor->screen);
	editor->highlight = WMCreateRGBColor(editor->screen, 0x4000, 0xc000, 0xffff, True);
	editor->gold = WMCreateRGBColor(editor->screen, 0xffff, 0xd700, 0, True);
	editor->starGray = WMGrayColor(editor->screen);
	editor->window = WMCreateWindow(editor->screen, "wmmonitors");
	WMSetWindowTitle(editor->window, _("Monitor Configuration"));
	WMSetWindowCloseAction(editor->window, closeEditor, editor);
	editor->column = WMCreateBox(editor->window);
	WMSetBoxBorderWidth(editor->column, 12);
	label = WMCreateLabel(editor->column);
	editor->hintLabel = label;
	WMSetLabelText(label, _("Drag to arrange; click the bottom-left star for primary, or the bottom-right checkbox to enable. Right-click for monitor settings."));
	WMSetLabelWraps(label, True);
	WMPackBoxSubview(editor->column, label, wmksize(560, 2 * WMFontHeight(editor->font)), False, 8);
	editor->canvas = WMCreateFrame(editor->column);
	WMSetFrameRelief(editor->canvas, WRFlat);
	WMPackBoxSubview(editor->column, editor->canvas,
			 wmksize(560, WMAX(360, 10 * (int)WMFontHeight(editor->font) + 2 * controlSize(editor) + 100)),
			 True, 8);
	WMCreateEventHandler(WMWidgetView(editor->canvas),
			     ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | Button1MotionMask,
			     canvasEvent, editor);
	editor->status = WMCreateLabel(editor->column);
	WMSetLabelWraps(editor->status, True);
	WMPackBoxSubview(editor->column, editor->status, wmksize(560, 3 * WMFontHeight(editor->font)), False, 8);
	actions = WMCreateBox(editor->column);
	editor->actions = actions;
	WMSetBoxHorizontal(actions, True);
	editor->load = actionButton(actions, _("Load Layout"), loadLayout, editor);
	editor->save = actionButton(actions, _("Save Layout"), saveLayout, editor);
	editor->refresh = actionButton(actions, _("Refresh"), refreshClicked, editor);
	editor->apply = actionButton(actions, _("Apply"), applyChanges, editor);
	editor->keep = actionButton(actions, _("Keep"), keepChanges, editor);
	editor->revert = actionButton(actions, _("Revert"), revertChanges, editor);
	actionButton(actions, _("Close"), closeEditor, editor);
	WMPackBoxSubview(editor->column, actions, WMGetBoxMinimumSize(actions), False, 0);
	editor->options = WMCreatePanelForWindow(editor->window, "monitorOptions");
	WMSetWindowCloseAction(editor->options, closeOptions, editor);
	options = WMCreateBox(editor->options);
	WMSetBoxBorderWidth(options, 12);
	label = WMCreateLabel(options);
	WMSetLabelText(label, _("Resolution and refresh rate"));
	WPrefsPackLabel(options, label, 4);
	editor->mode = WMCreatePopUpButton(options);
	WMAddPopUpButtonItem(editor->mode, "1920x1080 (60.00 Hz)");
	WMSetPopUpButtonAction(editor->mode, optionChanged, editor);
	WMPackBoxSubview(options, editor->mode, WMGetPopUpButtonPreferredSize(editor->mode), False, 8);
	label = WMCreateLabel(options);
	WMSetLabelText(label, _("Rotation"));
	WPrefsPackLabel(options, label, 4);
	editor->rotation = WMCreatePopUpButton(options);
	for (i = 0; i < wlengthof(rotations); i++)
		WMAddPopUpButtonItem(editor->rotation, rotations[i]);
	WMSetPopUpButtonAction(editor->rotation, optionChanged, editor);
	WMPackBoxSubview(options, editor->rotation, WMGetPopUpButtonPreferredSize(editor->rotation), False, 8);
	editor->flipX = WMCreateSwitchButton(options);
	WMSetButtonText(editor->flipX, _("Reflect horizontally"));
	WMSetButtonAction(editor->flipX, optionChanged, editor);
	WPrefsPackButton(options, editor->flipX, 4);
	editor->flipY = WMCreateSwitchButton(options);
	WMSetButtonText(editor->flipY, _("Reflect vertically"));
	WMSetButtonAction(editor->flipY, optionChanged, editor);
	WPrefsPackButton(options, editor->flipY, 8);
	actionButton(options, _("Done"), closeOptions, editor);
	size = WMGetBoxMinimumSize(options);
	WMResizeWidget(editor->options, size.width, size.height);
	WMResizeWidget(options, size.width, size.height);
	WMSetViewExpandsToParent(WMWidgetView(options), 0, 0, 0, 0);
	WMRealizeWidget(editor->options);
	WMMapSubwidgets(options);
	WMMapSubwidgets(editor->options);
	editor->confirmation = WMCreatePanelForWindow(editor->window, "confirmMonitors");
	WMSetWindowTitle(editor->confirmation, _("Keep monitor layout?"));
	WMSetWindowCloseAction(editor->confirmation, revertChanges, editor);
	confirmation = WMCreateBox(editor->confirmation);
	WMSetBoxBorderWidth(confirmation, 12);
	editor->confirmationLabel = WMCreateLabel(confirmation);
	WMSetLabelWraps(editor->confirmationLabel, True);
	WMPackBoxSubview(confirmation, editor->confirmationLabel,
			 wmksize(300, 3 * WMFontHeight(editor->font)), False, 8);
	confirmationActions = WMCreateBox(confirmation);
	WMSetBoxHorizontal(confirmationActions, True);
	actionButton(confirmationActions, _("Keep"), keepChanges, editor);
	actionButton(confirmationActions, _("Revert"), revertChanges, editor);
	WMPackBoxSubview(confirmation, confirmationActions, WMGetBoxMinimumSize(confirmationActions), False, 0);
	size = WMGetBoxMinimumSize(confirmation);
	WMResizeWidget(editor->confirmation, size.width, size.height);
	WMResizeWidget(confirmation, size.width, size.height);
	WMSetViewExpandsToParent(WMWidgetView(confirmation), 0, 0, 0, 0);
	WMRealizeWidget(editor->confirmation);
	WMMapSubwidgets(confirmationActions);
	WMMapSubwidgets(confirmation);
	WMMapSubwidgets(editor->confirmation);
	editor->live = editor->draft;
	editor->draft = MonitorLayoutCopy(editor->live);
	grabBackgrounds(editor);
	rebuild(editor);
	size = WMGetBoxMinimumSize(editor->column);
	WMSetWindowMinSize(editor->window, size.width, size.height);
	size.width = WMAX(size.width, 780);
	size.height = WMAX(size.height, 560);
	WMResizeWidget(editor->window, size.width, size.height);
	WMResizeWidget(editor->column, size.width, size.height);
	WMSetViewExpandsToParent(WMWidgetView(editor->column), 0, 0, 0, 0);
	WMRealizeWidget(editor->window);
	WMMapSubwidgets(actions);
	WMMapSubwidgets(editor->column);
	WMMapSubwidgets(editor->window);
	WMMapWidget(editor->window);
	WMSetLabelText(editor->status, _("Apply changes only for this session. Save Layout and Load Layout are explicit; nothing is restored at login."));
	paint(editor);
}
#endif

static void printHelp(const char *program)
{
	printf(_("usage: %s [options]\n"), program);
	puts(_("Arrange, enable and rotate monitors with a WINGs interface."));
	puts("");
	puts(_(" -display <display>      display to be used"));
	puts(_(" -h, --help              print this message and exit"));
	puts(_(" -v, --version           print version number and exit"));
}

int main(int argc, char **argv)
{
	const char *displayName = "";
	int i;
#ifdef USE_RANDR
	MonitorEditor editor = { 0 };
	int errorBase, major = 1, minor = 3;
	char error[512];
#endif

	setlocale(LC_ALL, "");
#ifdef I18N
	bindtextdomain("WPrefs", getenv("NLSPATH") ? getenv("NLSPATH") : LOCALEDIR);
	bind_textdomain_codeset("WPrefs", "UTF-8");
	textdomain("WPrefs");
#endif
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printHelp(argv[0]);
			return 0;
		} else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
			printf("wmmonitors (Window Maker) %s\n", VERSION);
			return 0;
		} else if (!strcmp(argv[i], "-display") && i + 1 < argc) {
			displayName = argv[++i];
		} else {
			printHelp(argv[0]);
			return 1;
		}
	}
#ifndef USE_RANDR
	(void)displayName;
	wwarning(_("Monitor configuration requires a build with RandR support."));
	return 1;
#else
	WMInitializeApplication("wmmonitors", &argc, argv);
	editor.dpy = XOpenDisplay(displayName);
	if (!editor.dpy) {
		wwarning(_("Could not open display %s."), XDisplayName(displayName));
		return 1;
	}
	if (!XRRQueryExtension(editor.dpy, &editor.eventBase, &errorBase) ||
	    !XRRQueryVersion(editor.dpy, &major, &minor) || major < 1 || (major == 1 && minor < 3)) {
		wwarning(_("Monitor configuration requires RandR 1.3 or newer."));
		XCloseDisplay(editor.dpy);
		return 1;
	}
	editor.root = DefaultRootWindow(editor.dpy);
	editor.draft = MonitorLayoutQuery(editor.dpy, editor.root, error, sizeof(error));
	if (!editor.draft) {
		wwarning("%s", error);
		XCloseDisplay(editor.dpy);
		return 1;
	}
	editor.screen = WMCreateScreen(editor.dpy, DefaultScreen(editor.dpy));
	if (!editor.screen) {
		wwarning(_("Could not initialize WINGs."));
		return 1;
	}
	editor.path = wdefaultspathfordomain("WMMonitorLayout");
	editor.dragging = editor.selected = -1;
	activeEditor = &editor;
	createEditor(&editor);
	WMHookEventHandler(eventHook);
	XRRSelectInput(editor.dpy, editor.root, RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask |
		       RROutputChangeNotifyMask | RROutputPropertyNotifyMask);
	WMScreenMainLoop(editor.screen);
	return 0;
#endif
}
