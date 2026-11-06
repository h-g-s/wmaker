#include "WPrefs.h"
#include "FontControls.h"
#include <math.h>

static int scaledSize(WMFont *font, int size)
{
	return size * WMAX(17, WMFontHeight(font)) / 17;
}

static WMPixmap *resizePreview(WMPixmap *source, int width, int height, RColor background)
{
	RImage *image, *scaled;
	WMPixmap *result;

	if (!source)
		return NULL;
	if (source->width == width && source->height == height)
		return WMRetainPixmap(source);
	image = RCreateImageFromDrawable(source->screen->rcontext, source->pixmap, source->mask);
	if (!image)
		goto failed;
	RCombineImageWithColor(image, &background);
	scaled = RSmoothScaleImage(image, width, height);
	RReleaseImage(image);
	if (!scaled)
		goto failed;
	result = WMCreatePixmapFromRImage(source->screen, scaled, 127);
	RReleaseImage(scaled);
	if (result)
		return result;
failed:
	wwarning(_("could not resize preference preview: %s"), RMessageForError(RErrorCode));
	return NULL;
}

void WPrefsSetPreviewButtonFont(WMButton *button, WMFont *font, WMPixmap *normal, WMPixmap *selected)
{
	int side = scaledSize(font, 48);
	RColor background = WMGetRColorFromColor(WMGetWidgetBackgroundColor(button));
	WMPixmap *image = resizePreview(normal, side, side, background);
	WMPixmap *altImage = resizePreview(selected, side, side, background);
	WMSize size;

	WMSetButtonFont(button, font);
	WMSetButtonImagePosition(button, WIPImageOnly);
	WMSetButtonImage(button, image);
	WMSetButtonAltImage(button, altImage);
	if (image)
		WMReleasePixmap(image);
	if (altImage)
		WMReleasePixmap(altImage);
	size = WMGetButtonPreferredSize(button);
	WMResizeWidget(button, size.width, size.height);
}

void WPrefsSetImageButtonFont(WMButton *button, WMFont *font, WMPixmap *normal, WMPixmap *selected)
{
	RColor background = WMGetRColorFromColor(WMGetWidgetBackgroundColor(button));
	WMPixmap *sources[] = { normal, selected };
	WMSize size;

	WMSetButtonFont(button, font);
	for (int i = 0; i < 2; i++) {
		WMPixmap *image = sources[i] ? resizePreview(sources[i], scaledSize(font, sources[i]->width),
							    scaledSize(font, sources[i]->height), background) : NULL;

		if (i)
			WMSetButtonAltImage(button, image);
		else
			WMSetButtonImage(button, image);
		if (image)
			WMReleasePixmap(image);
	}
	size = WMGetButtonPreferredSize(button);
	WMResizeWidget(button, size.width, size.height);
}

static WMPixmap *drawSpeedImage(WMButton *button, WMFont *font, int value, Bool selected)
{
	enum { samples = 4 };
	WMScreen *screen = WMWidgetScreen(button);
	int width = scaledSize(font, 32), height = scaledSize(font, 20);
	RImage *image = RCreateImage(width * samples, height * samples, False);
	RImage *scaled;
	WMPixmap *pixmap = NULL;
	RColor background = WMGetRColorFromColor(WMGetWidgetBackgroundColor(button));
	int glyphHeight = scaledSize(font, 12) * samples;
	int bar = glyphHeight / 4, gap = glyphHeight / 8;
	int tip = glyphHeight / 2;
	int glyphWidth = (value + 1) * (bar + gap) + tip;
	int left = (width * samples - glyphWidth) / 2;
	int top = (height * samples - glyphHeight) / 2;
	int edge = WMAX(samples, glyphHeight / 14);
	int x, y;

	if (!image)
		goto failed;
	RFillImage(image, &background);
	for (y = 0; y < glyphHeight; y++) {
		for (x = 0; x < glyphWidth; x++) {
			int segment = x / (bar + gap);
			int offset = x % (bar + gap);
			int arrowX = x - (value + 1) * (bar + gap);
			int distance = abs(2 * y - glyphHeight + 1);
			Bool inBar = segment <= value && offset < bar;
			Bool inTip = arrowX >= 0 && 2 * arrowX + distance < glyphHeight;
			Bool border;
			unsigned char *pixel;

			if (!inBar && !inTip)
				continue;
			border = inBar ? offset < edge || offset >= bar - edge ||
				y < edge || y >= glyphHeight - edge :
				arrowX < edge || 2 * arrowX + distance >= glyphHeight - 3 * edge;
			pixel = image->data + 3 * ((top + y) * image->width + left + x);
			if (selected) {
				pixel[0] = pixel[1] = border ? 255 : 0;
				pixel[2] = 0;
			} else {
				Bool highlight = inBar ? offset < edge / 2 : arrowX < edge / 2;

				pixel[0] = highlight ? 86 : 35;
				pixel[1] = highlight ? 111 : 61;
				pixel[2] = highlight ? 167 : 122;
			}
		}
	}
	scaled = RSmoothScaleImage(image, width, height);
	RReleaseImage(image);
	if (!scaled)
		goto failed;
	pixmap = WMCreatePixmapFromRImage(screen, scaled, 127);
	RReleaseImage(scaled);
	if (pixmap)
		return pixmap;
failed:
	wwarning(_("could not render scaled speed indicator: %s"), RMessageForError(RErrorCode));
	return NULL;
}

static WMPixmap *speedImage(WMButton *button, WMFont *font, int value, Bool selected)
{
	WMScreen *screen = WMWidgetScreen(button);
	char name[16];
	char *path;
	WMPixmap *pixmap = NULL;

	if (WMFontHeight(font) > 17)
		return drawSpeedImage(button, font, value, selected);
	snprintf(name, sizeof(name), "speed%d%s", value, selected ? "s" : "");
	path = LocateImage(name);
	if (!path)
		return NULL;
	pixmap = WMCreatePixmapFromFile(screen, path);
	if (!pixmap)
		wwarning(_("could not load icon %s: %s"), path, RMessageForError(RErrorCode));
	wfree(path);
	return pixmap;
}

static WMPixmap *clockImage(WMButton *button, WMFont *font, int clock, Bool selected)
{
	WMScreen *screen = WMWidgetScreen(button);
	Display *display = WMScreenDisplay(screen);
	WMPixmap *result = NULL;
	RImage *image, *scaled;
	Pixmap canvas;
	GC gc;
	WMColor *sector;
	int size = scaledSize(font, 24), side = size * 4;
	int center = side / 2, radius = side * 9 / 24;
	int line = WMAX(4, side / 24);
	int top = center - radius;
	int i;

	if (WMFontHeight(font) <= 17) {
		char name[16];
		char *path;

		snprintf(name, sizeof(name), "timer%d%s", clock, selected ? "s" : "");
		path = LocateImage(name);
		if (!path)
			return NULL;
		result = WMCreatePixmapFromFile(screen, path);
		if (!result)
			wwarning(_("could not load icon %s: %s"), path, RMessageForError(RErrorCode));
		wfree(path);
		return result;
	}
	sector = WMCreateRGBColor(screen, selected ? 65535 : 0x5555,
				  selected ? 0 : 0x5555, selected ? 0 : 0x5555, True);
	canvas = XCreatePixmap(display, W_DRAWABLE(screen), side, side, WMScreenDepth(screen));
	gc = XCreateGC(display, canvas, 0, NULL);
	XSetForeground(display, gc, WMColorPixel(WMGetWidgetBackgroundColor(button)));
	XFillRectangle(display, canvas, gc, 0, 0, side, side);
	XSetForeground(display, gc, W_PIXEL(selected ? screen->white : screen->gray));
	XFillArc(display, canvas, gc, center - radius, top, 2 * radius, 2 * radius, 0, 360 * 64);
	if (clock) {
		XSetForeground(display, gc, WMColorPixel(sector));
		XFillArc(display, canvas, gc, center - radius, top, 2 * radius, 2 * radius,
			 90 * 64, -clock * 45 * 64);
	}
	XSetLineAttributes(display, gc, line, LineSolid, CapRound, JoinRound);
	XSetForeground(display, gc, W_PIXEL(screen->black));
	XDrawArc(display, canvas, gc, center - radius, top, 2 * radius, 2 * radius, 0, 360 * 64);
	/* Stopwatch crown and the two angled pushers. */
	XDrawLine(display, canvas, gc, center, top, center, top - 3 * line);
	XDrawLine(display, canvas, gc, center - 2 * line, top - 3 * line,
		  center + 2 * line, top - 3 * line);
	for (i = -1; i <= 1; i += 2) {
		XDrawLine(display, canvas, gc, center + i * radius * 2 / 3, top + radius / 4,
			  center + i * (radius * 2 / 3 + line), top + radius / 4 - 2 * line);
	}
	if (clock) {
		double angle = clock * 3.14159265358979323846 / 4;

		for (i = 0; i < 8; i++) {
			double tick = i * 3.14159265358979323846 / 4;

			XDrawLine(display, canvas, gc,
				  center + sin(tick) * (radius - line),
				  center - cos(tick) * (radius - line),
				  center + sin(tick) * (radius - 3 * line),
				  center - cos(tick) * (radius - 3 * line));
		}
		XDrawLine(display, canvas, gc, center, center, center, top + line);
		XDrawLine(display, canvas, gc, center, center,
			  center + sin(angle) * (radius - line),
			  center - cos(angle) * (radius - line));
	}
	WMReleaseColor(sector);
	image = RCreateImageFromDrawable(WMScreenRContext(screen), canvas, None);
	XFreeGC(display, gc);
	XFreePixmap(display, canvas);
	if (!image)
		goto failed;
	scaled = RSmoothScaleImage(image, size, size);
	RReleaseImage(image);
	if (!scaled)
		goto failed;
	result = WMCreatePixmapFromRImage(screen, scaled, 127);
	RReleaseImage(scaled);
	if (result)
		return result;
failed:
	wwarning(_("could not render scaled clock indicator: %s"), RMessageForError(RErrorCode));
	return NULL;
}

void WPrefsSetClockButtonFont(WMButton *button, WMFont *font, int clock)
{
	WMPixmap *normal, *selected;
	int cell = scaledSize(font, 28);

	assert(clock >= 0 && clock <= 5);
	normal = clockImage(button, font, clock, False);
	selected = clockImage(button, font, clock, True);
	WMSetButtonFont(button, font);
	WMSetButtonBordered(button, False);
	WMSetButtonImagePosition(button, WIPImageOnly);
	WMSetButtonImage(button, normal);
	WMSetButtonAltImage(button, selected);
	if (normal)
		WMReleasePixmap(normal);
	if (selected)
		WMReleasePixmap(selected);
	WMResizeWidget(button, cell, cell);
}

static WMPixmap *menuAlignmentImage(WMButton *button, WMFont *font, Bool aligned)
{
	WMScreen *screen = WMWidgetScreen(button);
	Display *display = WMScreenDisplay(screen);
	WMPixmap *result;
	RImage *image, *scaled;
	Pixmap canvas;
	GC gc;
	int size = scaledSize(font, 32), unit = WMAX(4, size / 8);
	int side = 32 * unit;
	int menu, row;

	if (WMFontHeight(font) <= 17) {
		char *path = LocateImage(aligned ? "menualign2" : "menualign1");

		if (!path)
			return NULL;
		result = WMCreatePixmapFromFile(screen, path);
		if (!result)
			wwarning(_("could not load icon %s: %s"), path, RMessageForError(RErrorCode));
		wfree(path);
		return result;
	}
	canvas = XCreatePixmap(display, W_DRAWABLE(screen), side, side, WMScreenDepth(screen));
	gc = XCreateGC(display, canvas, 0, NULL);
	XSetForeground(display, gc, WMColorPixel(WMGetWidgetBackgroundColor(button)));
	XFillRectangle(display, canvas, gc, 0, 0, side, side);
	for (menu = 0; menu < 2; menu++) {
		int x = (1 + menu * 15) * unit;
		int y = (aligned ? 3 : 1 + menu * 10) * unit;
		int rows = menu ? 4 : 5;
		int width = 15 * unit, height = (rows * 5 + 1) * unit;

		XSetForeground(display, gc, W_PIXEL(screen->black));
		XFillRectangle(display, canvas, gc, x, y, width, height);
		for (row = 0; row < rows; row++) {
			int top = y + (1 + row * 5) * unit;

			if (row == 0) {
				XSetForeground(display, gc, W_PIXEL(screen->darkGray));
				XFillRectangle(display, canvas, gc, x + unit, top, width - 2 * unit, 4 * unit);
				XSetForeground(display, gc, W_PIXEL(screen->gray));
				XFillRectangle(display, canvas, gc, x + unit, top, width - 2 * unit, unit);
				XFillRectangle(display, canvas, gc, x + unit, top, unit, 4 * unit);
				continue;
			}
			XSetForeground(display, gc, W_PIXEL(screen->gray));
			XFillRectangle(display, canvas, gc, x + unit, top, width - 2 * unit, 4 * unit);
			XSetForeground(display, gc, W_PIXEL(screen->white));
			XFillRectangle(display, canvas, gc, x + unit, top, width - 2 * unit, unit);
			XFillRectangle(display, canvas, gc, x + unit, top, unit, 4 * unit);
			XSetForeground(display, gc, W_PIXEL(screen->darkGray));
			XFillRectangle(display, canvas, gc, x + width - 2 * unit, top, unit, 4 * unit);
		}
	}
	image = RCreateImageFromDrawable(WMScreenRContext(screen), canvas, None);
	XFreeGC(display, gc);
	XFreePixmap(display, canvas);
	if (!image)
		goto failed;
	scaled = RSmoothScaleImage(image, size, size);
	RReleaseImage(image);
	if (!scaled)
		goto failed;
	result = WMCreatePixmapFromRImage(screen, scaled, 127);
	RReleaseImage(scaled);
	if (result)
		return result;
failed:
	wwarning(_("could not render scaled menu alignment indicator: %s"), RMessageForError(RErrorCode));
	return NULL;
}

void WPrefsSetMenuAlignmentButtonFont(WMButton *button, WMFont *font, Bool aligned)
{
	WMPixmap *image = menuAlignmentImage(button, font, aligned);
	WMSize size;

	WMSetButtonFont(button, font);
	WMSetButtonImagePosition(button, WIPImageOnly);
	WMSetButtonImage(button, image);
	if (image)
		WMReleasePixmap(image);
	size = WMGetButtonPreferredSize(button);
	WMResizeWidget(button, size.width, size.height);
}

void WPrefsSetSpeedControlFont(WPrefsSpeedControl *control, WMFont *font)
{
	int cell = scaledSize(font, 40);
	int i;

	for (i = 0; i < 5; i++) {
		WMPixmap *normal = speedImage(control->buttons[i], font, i, False);
		WMPixmap *selected = speedImage(control->buttons[i], font, i, True);

		WMSetButtonFont(control->buttons[i], font);
		WMSetButtonImage(control->buttons[i], normal);
		WMSetButtonAltImage(control->buttons[i], selected);
		if (normal)
			WMReleasePixmap(normal);
		if (selected)
			WMReleasePixmap(selected);
		WMResizeWidget(control->buttons[i], cell, cell);
		WMMoveWidget(control->buttons[i], i * cell, 0);
	}
	WMResizeWidget(control->box, 5 * cell, cell);
}

void WPrefsCreateSpeedControl(WPrefsSpeedControl *control, WMWidget *parent, WMFont *font)
{
	int i;

	control->box = WMCreateBox(parent);
	for (i = 0; i < 5; i++) {
		control->buttons[i] = WMCreateCustomButton(control->box, WBBStateChangeMask);
		WMSetButtonBordered(control->buttons[i], False);
		WMSetButtonImagePosition(control->buttons[i], WIPImageOnly);
		if (i)
			WMGroupButtons(control->buttons[0], control->buttons[i]);
	}
	WPrefsSetSpeedControlFont(control, font);
	WPrefsSetSpeedControlValue(control, 0);
	WMMapSubwidgets(control->box);
}

void WPrefsSetSpeedControlValue(WPrefsSpeedControl *control, int value)
{
	assert(value >= 0 && value < 5);
	WMSetButtonSelected(control->buttons[value], True);
}

int WPrefsGetSpeedControlValue(WPrefsSpeedControl *control)
{
	int i;

	for (i = 0; i < 5; i++) {
		if (WMGetButtonSelected(control->buttons[i]))
			return i;
	}
	return -1;
}

void WPrefsSetIconPositionControlValue(WPrefsIconPositionControl *control, int value)
{
	int w, h, x, y;
	WMSize size = WMGetViewSize(WMWidgetView(control->screen));

	assert(value >= 0 && value < 8);
	control->position = value;
	WMSetButtonSelected(control->buttons[value], True);
	w = value & 1 ? 4 * control->unit : control->unit;
	h = value & 1 ? control->unit : 4 * control->unit;
	x = value & 2 ? (int)size.width - 2 - w : 2;
	y = value & 4 ? (int)size.height - 2 - h : 2;
	WMResizeWidget(control->marker, w, h);
	WMMoveWidget(control->marker, x, y);
}

static void iconPositionChanged(WMWidget *widget, void *data)
{
	WPrefsIconPositionControl *control = data;
	int i;

	for (i = 0; i < 8; i++) {
		if (widget == control->buttons[i]) {
			WPrefsSetIconPositionControlValue(control, i);
			break;
		}
	}
}

static void arrangeIconPosition(WPrefsIconPositionControl *control)
{
	WMScreen *screen = WMWidgetScreen(control->box);
	int screenWidth = WMScreenWidth(screen);
	int screenHeight = WMScreenHeight(screen);
	int outerWidth = WMWidgetWidth(control->box), outerHeight = WMWidgetHeight(control->box);
	int button = control->buttonSize, gap = control->gap;
	int step = button + gap;
	int width = WMAX(4 * control->unit + 4, outerWidth - 2 * step);
	int height = WMAX(4 * control->unit + 4, outerHeight - 2 * step);
	int startx, starty, i;

	if ((long)width * screenHeight > (long)height * screenWidth)
		width = height * screenWidth / screenHeight;
	else
		height = width * screenHeight / screenWidth;
	width = WMAX(4 * control->unit + 4, width);
	height = WMAX(4 * control->unit + 4, height);
	startx = (outerWidth - width - 2 * step) / 2;
	starty = (outerHeight - height - 2 * step) / 2;
	for (i = 0; i < 8; i++) {
		int w = i & 1 ? width / 2 : button;
		int h = i & 1 ? button : height / 2;
		int x = startx, y = starty;

		WMResizeWidget(control->buttons[i], w, h);
		switch (i) {
		case 0: y += step; break;
		case 1: x += step; break;
		case 2: x += step + width; y += step; break;
		case 3: x += step + w; break;
		case 4: y += step + h; break;
		case 5: x += step; y += step + height; break;
		case 6: x += step + width; y += step + h; break;
		case 7: x += step + w; y += step + height; break;
		}
		WMMoveWidget(control->buttons[i], x, y);
	}
	WMResizeWidget(control->screen, width, height);
	WMMoveWidget(control->screen, startx + step, starty + step);
	WPrefsSetIconPositionControlValue(control, control->position);
}

static void iconPositionResized(void *data, WMNotification *notification)
{
	(void)notification;
	arrangeIconPosition(data);
}

static void iconPositionEvents(XEvent *event, void *data)
{
	if (event->type == DestroyNotify)
		WMRemoveNotificationObserver(data);
}

void WPrefsSetIconPositionControlFont(WPrefsIconPositionControl *control, WMFont *font)
{
	control->unit = scaledSize(font, 8);
	control->buttonSize = scaledSize(font, 14);
	control->gap = scaledSize(font, 1);
	for (int i = 0; i < 8; i++)
		WMSetButtonFont(control->buttons[i], font);
	WMResizeWidget(control->box, scaledSize(font, 240), scaledSize(font, 122));
	arrangeIconPosition(control);
}

void WPrefsCreateIconPositionControl(WPrefsIconPositionControl *control, WMWidget *parent, WMFont *font)
{
	WMColor *color;
	int i;

	control->box = WMCreateBox(parent);
	control->position = 0;
	for (i = 0; i < 8; i++) {
		control->buttons[i] = WMCreateButton(control->box, WBTOnOff);
		WMSetButtonAction(control->buttons[i], iconPositionChanged, control);
		if (i)
			WMGroupButtons(control->buttons[0], control->buttons[i]);
	}
	control->screen = WMCreateFrame(control->box);
	WMSetFrameRelief(control->screen, WRSunken);
	color = WMCreateRGBColor(WMWidgetScreen(parent), 0x5100, 0x5100, 0x7100, True);
	WMSetWidgetBackgroundColor(control->screen, color);
	WMReleaseColor(color);
	control->marker = WMCreateFrame(control->screen);
	WMSetFrameRelief(control->marker, WRSimple);
	WPrefsSetIconPositionControlFont(control, font);
	WMAddNotificationObserver(iconPositionResized, control, WMViewSizeDidChangeNotification,
				  WMWidgetView(control->box));
	WMSetViewNotifySizeChanges(WMWidgetView(control->box), True);
	WMCreateEventHandler(WMWidgetView(control->box), StructureNotifyMask, iconPositionEvents, control);
	WMMapSubwidgets(control->screen);
	WMMapSubwidgets(control->box);
}
