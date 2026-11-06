#include "WPrefs.h"
#include "FontControls.h"
#include "PanelLayout.h"

#undef NDEBUG
#include <assert.h>

#include "Focus.c"

static WMPropList *focusDomain;

char *GetStringForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMGetFromPLDictionary(focusDomain, name);

	WMReleasePropList(name);
	return value ? WMGetFromPLString(value) : NULL;
}

int GetIntegerForKey(const char *key)
{
	char *value = GetStringForKey(key);

	return value ? atoi(value) : 0;
}

Bool GetBoolForKey(const char *key)
{
	char *value = GetStringForKey(key);

	return value && strcmp(value, "YES") == 0;
}

void SetStringForKey(const char *value, const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *text = WMCreatePLString(value);

	WMPutInPLDictionary(focusDomain, name, text);
	WMReleasePropList(name);
	WMReleasePropList(text);
}

void SetIntegerForKey(int value, const char *key)
{
	char text[32];

	sprintf(text, "%d", value);
	SetStringForKey(text, key);
}

void SetBoolForKey(Bool value, const char *key)
{
	SetStringForKey(value ? "YES" : "NO", key);
}

void AddSection(Panel *panel, const char *iconFile)
{
	(void)panel;
	(void)iconFile;
}

static void checkFocusBounds(WMView *view)
{
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		if (child->self && WMWidgetClass(child->self) == WC_Button)
			assert(WMGetButtonPreferredSizeForWidth(child->self, child->size.width).height
			       <= child->size.height);
		checkFocusBounds(child);
	}
}

static void testFocusPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	int presets[] = { 0, 10, 100, 350, 800 };
	WMFont *original = WMDefaultSystemFont(screen);
	WMSize previous = { 0, 0 };

	focusDomain = WMCreatePLDictionary(NULL, NULL);
	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMWindow *window;
		_Panel *panel;
		WMSize size;

		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		SetStringForKey("clicktofocus", "FocusMode");
		SetStringForKey("focusfollowsmouse", "ColormapMode");
		SetIntegerForKey(137, "RaiseDelay");
		window = WMCreateWindow(screen, "focusTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitFocus(window);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		checkFocusBounds(WMWidgetView(panel->box));
		assert(WMGetButtonSelected(panel->kfocB[0]));
		assert(WMGetButtonSelected(panel->autB));
		for (int j = 0; j < 5; j++)
			assert(!WMGetButtonSelected(panel->raisB[j]));
		storeData(panel);
		assert(GetIntegerForKey("RaiseDelay") == 137);
		for (int j = 0; j < 5; j++) {
			WMPerformButtonClick(panel->raisB[j]);
			storeData(panel);
			assert(GetIntegerForKey("RaiseDelay") == presets[j]);
		}
		WMSetTextFieldText(panel->raisT, "4321");
		WMPostNotificationName(WMTextDidChangeNotification, panel->raisT, NULL);
		for (int j = 0; j < 5; j++)
			assert(!WMGetButtonSelected(panel->raisB[j]));
		WMPerformButtonClick(panel->kfocB[1]);
		WMPerformButtonClick(panel->manB);
		WMSetButtonSelected(panel->ignB, True);
		WMSetButtonSelected(panel->newB, True);
		WMSetButtonSelected(panel->craisB, True);
		storeData(panel);
		assert(GetIntegerForKey("RaiseDelay") == 4321);
		assert(strcmp(GetStringForKey("FocusMode"), "sloppy") == 0);
		assert(strcmp(GetStringForKey("ColormapMode"), "manual") == 0);
		assert(GetBoolForKey("IgnoreFocusClick") && GetBoolForKey("AutoFocus") &&
		       GetBoolForKey("CirculateRaise"));
		WMRemoveNotificationObserver(panel);
		WMDestroyWidget(window);
		wfree(panel);
	}
	WMSetWidgetDefaultFont(screen, original);
	WMReleaseFont(original);
	WMReleasePropList(focusDomain);
}

char *LocateImage(const char *name)
{
	char *path = wmalloc(strlen(TEST_IMAGE_PATH) + strlen(name) + 7);

	sprintf(path, "%s/%s.tiff", TEST_IMAGE_PATH, name);
	return path;
}

static void checkPositions(WPrefsIconPositionControl *control)
{
	int i, j;
	WMSize screen = WMGetViewSize(WMWidgetView(control->screen));

	for (i = 0; i < 8; i++) {
		WMSize marker;
		WMPoint position;

		WMPerformButtonClick(control->buttons[i]);
		assert(control->position == i);
		marker = WMGetViewSize(WMWidgetView(control->marker));
		position = WMGetViewPosition(WMWidgetView(control->marker));
		assert(position.x >= 2 && position.y >= 2);
		assert(position.x + marker.width <= screen.width - 2);
		assert(position.y + marker.height <= screen.height - 2);
		assert((marker.width > marker.height) == (i & 1));
		assert((position.x == 2) == !(i & 2));
		assert((position.y == 2) == !(i & 4));
		for (j = 0; j < 8; j++)
			assert(WMGetButtonSelected(control->buttons[j]) == (i == j));
	}
}

static int symbolHeight(WMButton *button, RColor color)
{
	RImage *image = RCreateImageFromDrawable(WMScreenRContext(WMWidgetScreen(button)),
					       WMWidgetXID(button), None);
	int x, y, first = -1, last = -1;

	assert(image);
	for (y = 0; y < image->height; y++) {
		for (x = 0; x < image->width; x++) {
			unsigned char *pixel = image->data + (image->format == RRGBAFormat ? 4 : 3)
				* (y * image->width + x);

			if (abs(pixel[0] - color.red) <= 2 && abs(pixel[1] - color.green) <= 2 &&
			    abs(pixel[2] - color.blue) <= 2) {
				if (first < 0)
					first = y;
				last = y;
			}
		}
	}
	RReleaseImage(image);
	return first < 0 ? 0 : last - first + 1;
}

static void checkSpeed(WPrefsSpeedControl *control)
{
	int i, j;
	RColor yellow = { 255, 255, 0, 255 };
	RColor blue = { 35, 61, 122, 255 };

	for (i = 0; i < 5; i++) {
		WMSize size;
		WMSize button;
		WMPoint position;
		XImage *image;
		WMScreen *screen = WMWidgetScreen(control->box);
		XColor background;
		RColor expected = WMGetRColorFromColor(screen->gray);
		int minimumHeight;

		WMPerformButtonClick(control->buttons[i]);
		assert(WPrefsGetSpeedControlValue(control) == i);
		for (j = 0; j < 5; j++)
			assert(WMGetButtonSelected(control->buttons[j]) == (i == j));
		size = WMGetViewSize(WMWidgetView(control->box));
		button = WMGetViewSize(WMWidgetView(control->buttons[i]));
		assert(WMGetButtonPreferredSize(control->buttons[i]).width <= button.width);
		assert(WMGetButtonPreferredSize(control->buttons[i]).height <= button.height);
		position = WMGetViewPosition(WMWidgetView(control->buttons[i]));
		assert(position.x + button.width <= size.width);
		assert(position.y + button.height <= size.height);
		image = XGetImage(WMScreenDisplay(screen), WMWidgetXID(control->buttons[i]),
				  0, 0, button.width, button.height, AllPlanes, ZPixmap);
		assert(image);
		background.pixel = XGetPixel(image, button.width / 2, button.height / 4);
		XQueryColor(WMScreenDisplay(screen), screen->colormap, &background);
		/* Allow integer rounding in the scaler's two filtering passes. */
		assert(abs((background.red >> 8) - expected.red) <= 2);
		assert(abs((background.green >> 8) - expected.green) <= 2);
		assert(abs((background.blue >> 8) - expected.blue) <= 2);
		minimumHeight = button.height > 40 ? button.height * 12 / 40 - 3 : 1;
		XDestroyImage(image);
		assert(symbolHeight(control->buttons[i], yellow) >= minimumHeight);
		WMSetButtonSelected(control->buttons[i], False);
		assert(symbolHeight(control->buttons[i], blue) >= minimumHeight);
		WMSetButtonSelected(control->buttons[i], True);
	}
}

static int done;

static void finish(void *data)
{
	WMWindow *window = data;
	XEvent event = { 0 };

	done = 1;
	event.xclient.type = ClientMessage;
	event.xclient.display = WMScreenDisplay(WMWidgetScreen(window));
	event.xclient.window = WMWidgetXID(window);
	event.xclient.format = 32;
	XSendEvent(event.xclient.display, event.xclient.window, False, 0, &event);
	XFlush(event.xclient.display);
}

static void drainEvents(Display *display)
{
	int count = 0;

	XSync(display, False);
	while (XPending(display)) {
		XEvent event;

		assert(++count < 10000);
		XNextEvent(display, &event);
		WMHandleEvent(&event);
		XSync(display, False);
	}
}

static void testPanelLayout(WMScreen *screen)
{
	WMWindow *window = WMCreateWindow(screen, "panelLayout");
	WMScrollView *sections = WMCreateScrollView(window);
	WMScrollView *viewport = WMCreateScrollView(window);
	WMFrame *content = WMCreateFrame(window);
	WMButton *help = WMCreateSwitchButton(window);
	WMButton *save = WMCreateCommandButton(window);
	WMButton *close = WMCreateCommandButton(window);
	WPrefsWindowLayout layout = { window, sections, viewport, content, help, save, close };
	WMBox *panel = WPrefsCreatePanelBox(content);
	WPrefsGroup group = WPrefsCreateGroup(panel, "A translated group heading");
	WMButton *option = WMCreateSwitchButton(group.content);
	WMSize size, before;
	WMPoint position;
	WMFont *font = WMSystemFontOfSize(screen, 32);

	WMSetButtonFont(option, font);
	WMSetButtonText(option, "A larger-font preference that determines the group's minimum width");
	WPrefsPackButton(group.content, option, 0);
	WPrefsPackGroup(panel, &group, True, 0);
	size = WMGetBoxMinimumSize(panel);
	WMResizeWidget(panel, size.width, size.height);
	assert(WMWidgetWidth(group.frame) >= WMGetButtonPreferredSize(option).width + 20);
	position = WMGetViewPosition(WMWidgetView(group.content));
	assert(position.x == 10);
	assert(position.y > WMFontHeight(screen->normalFont));
	assert(WMWidgetHeight(group.content) >= WMGetButtonPreferredSize(option).height);
	WMReleaseFont(font);
	WMDestroyWidget(panel);

	WMSetButtonText(help, "Balloon Help");
	WMSetButtonText(save, "Save");
	WMSetButtonText(close, "Close");
	WMResizeWidget(sections, 500, 87);
	WMResizeWidget(viewport, 600, 400);
	WMResizeWidget(content, 600, 400);
	WMSetScrollViewRelief(viewport, WRFlat);
	WMSetScrollViewContentView(viewport, WMWidgetView(content));
	WMRealizeWidget(window);
	WMMapWidget(window);
	WPrefsArrangeWindow(&layout, wmksize(600, 400));
	drainEvents(WMScreenDisplay(screen));
	before = WMGetViewSize(WMWidgetView(window));
	assert(before.width <= WMScreenWidth(screen) - 40);
	assert(before.height <= WMScreenHeight(screen) - 80);
	assert(!WMGetScrollViewHorizontalScroller(viewport));
	assert(!WMGetScrollViewVerticalScroller(viewport));
	WMResizeWidget(window, 360, 360);
	drainEvents(WMScreenDisplay(screen));
	assert(WMGetScrollViewHorizontalScroller(viewport));
	assert(WMGetScrollViewVerticalScroller(viewport));
	assert(WMWidgetWidth(content) >= 600 && WMWidgetHeight(content) >= 400);
	assert(WMGetViewPosition(WMWidgetView(close)).x >=
	       WMGetViewPosition(WMWidgetView(save)).x + WMWidgetWidth(save));
	assert(WMGetViewPosition(WMWidgetView(save)).x >=
	       WMGetViewPosition(WMWidgetView(help)).x + WMWidgetWidth(help));
	assert(WMWidgetHeight(close) >= WMGetButtonPreferredSize(close).height);
	assert(W_VIEW(close)->parent->pos.y + WMWidgetHeight(close) <= 360);
	WMScrollViewScrollPoint(viewport, wmkpoint(200, 80));
	assert(WMGetScrollViewVisibleRect(viewport).pos.x == 200);
	assert(WMGetScrollViewVisibleRect(viewport).pos.y == 80);
	WMResizeWidget(window, before.width, before.height);
	drainEvents(WMScreenDisplay(screen));
	assert(!WMGetScrollViewHorizontalScroller(viewport));
	assert(!WMGetScrollViewVerticalScroller(viewport));
	assert(WMGetScrollViewVisibleRect(viewport).pos.x == 0);
	assert(WMGetScrollViewVisibleRect(viewport).pos.y == 0);
	WMDestroyWidget(window);
}

static void testPreviewButton(WMScreen *screen)
{
	WMWindow *window = WMCreateWindow(screen, "previewTest");
	WMButton *button = WMCreateButton(window, WBTToggle);
	WMFont *small = WMSystemFontOfSize(screen, 11);
	WMFont *large = WMSystemFontOfSize(screen, 32);
	char *path = LocateImage("dock-preview");
	WMPixmap *normal = WMCreatePixmapFromFile(screen, path);
	WMPixmap *disabled = WMCreatePixmap(screen, 192, 192, screen->depth, True);
	WMSize baseline;
	GC gc;

	wfree(path);
	assert(normal && disabled);
	gc = XCreateGC(screen->display, disabled->pixmap, 0, NULL);
	XSetForeground(screen->display, gc, WMColorPixel(screen->black));
	XFillRectangle(screen->display, disabled->pixmap, gc, 0, 0, 192, 192);
	XFreeGC(screen->display, gc);
	gc = XCreateGC(screen->display, disabled->mask, 0, NULL);
	XSetForeground(screen->display, gc, 0);
	XFillRectangle(screen->display, disabled->mask, gc, 0, 0, 192, 192);
	XFreeGC(screen->display, gc);
	WPrefsSetPreviewButtonFont(button, small, disabled, normal);
	baseline = WMGetButtonPreferredSize(button);
	WMSetButtonSelected(button, True);
	WPrefsSetPreviewButtonFont(button, large, disabled, normal);
	assert(WMGetButtonSelected(button));
	assert(WMGetButtonPreferredSize(button).width > baseline.width);
	assert(WMWidgetWidth(button) == WMGetButtonPreferredSize(button).width);
	WMRealizeWidget(window);
	WMSetButtonSelected(button, False);
	WMMapWidget(button);
	WMMapWidget(window);
	XSync(screen->display, False);
	drainEvents(screen->display);
	{
		RImage *pixels = RCreateImageFromDrawable(screen->rcontext, WMWidgetXID(button), None);
		RColor background = WMGetRColorFromColor(WMGetWidgetBackgroundColor(button));
		unsigned char *pixel;

		assert(pixels);
		pixel = pixels->data + 3 * ((pixels->height / 2) * pixels->width + pixels->width / 2);
		assert(abs(pixel[0] - background.red) <= 2);
		assert(abs(pixel[1] - background.green) <= 2);
		assert(abs(pixel[2] - background.blue) <= 2);
		RReleaseImage(pixels);
	}
	WMSetButtonSelected(button, True);
	WMSetButtonSelected(button, False);
	WPrefsSetPreviewButtonFont(button, small, disabled, normal);
	assert(!WMGetButtonSelected(button));
	assert(WMGetButtonPreferredSize(button).width == baseline.width);
	WMReleasePixmap(normal);
	WMReleasePixmap(disabled);
	WMReleaseFont(small);
	WMReleaseFont(large);
	WMDestroyWidget(window);
}

int main(int argc, char **argv)
{
	Display *display;
	WMScreen *screen;
	WMWindow *window;
	WMBox *row;
	WPrefsSpeedControl speeds[3];
	WPrefsIconPositionControl positions[3];
	WMButton *clocks[3][2][6];
	WMButton *alignments[3][2];
	int sizes[] = { 11, 24, 32 };
	WMSize total;
	int i;
	Bool gallery = argc == 2 && strcmp(argv[1], "--gallery") == 0;

	if (!getenv("DISPLAY")) {
		puts("SKIP: font_controls_test needs an X server");
		return 77;
	}
	WMInitializeApplication("font_controls_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	if (!gallery) {
		testPanelLayout(screen);
		testPreviewButton(screen);
		testFocusPanel(screen);
	}
	window = WMCreateWindow(screen, "fontControls");
	WMSetWindowTitle(window, "Font-aware sliders, speed choices, and icon positioning");
	row = WMCreateBox(window);
	WMSetBoxHorizontal(row, True);
	WMSetBoxBorderWidth(row, 20);
	for (i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMBox *column = WMCreateBox(row);
		WMLabel *label = WMCreateLabel(column);
		WMSlider *slider = WMCreateSlider(column);
		WMSize size;
		char title[32];

		assert(font);
		snprintf(title, sizeof(title), "Font size %d", sizes[i]);
		WMSetLabelText(label, title);
		WMSetLabelFont(label, font);
		WMPackBoxSubview(column, label, WMGetLabelPreferredSize(label), False, 16);
		WMSetSliderFont(slider, font);
		WMSetSliderValue(slider, 30);
		WMPackBoxSubview(column, slider, WMGetSliderPreferredSize(slider), False, 16);
		WPrefsCreateSpeedControl(&speeds[i], column, font);
		WPrefsSetSpeedControlValue(&speeds[i], 3);
		size = WMGetViewSize(WMWidgetView(speeds[i].box));
		WMPackBoxSubview(column, speeds[i].box, size, False, 16);
		for (int state = 0; state < 2; state++) {
			WMBox *clockRow = WMCreateBox(column);

			WMSetBoxHorizontal(clockRow, True);
			for (int clock = 0; clock < 6; clock++) {
				WMButton *button = WMCreateCustomButton(clockRow, WBBStateChangeMask);

				clocks[i][state][clock] = button;
				WPrefsSetClockButtonFont(button, font, clock);
				WMSetButtonSelected(button, state);
				WMPackBoxSubview(clockRow, button, WMGetViewSize(WMWidgetView(button)), False, 2);
			}
			WMPackBoxSubview(column, clockRow, WMGetBoxMinimumSize(clockRow), False, 12);
			WMMapSubwidgets(clockRow);
		}
		{
			WMBox *alignmentRow = WMCreateBox(column);

			WMSetBoxHorizontal(alignmentRow, True);
			for (int aligned = 0; aligned < 2; aligned++) {
				WMButton *button = WMCreateButton(alignmentRow, WBTOnOff);

				alignments[i][aligned] = button;
				WPrefsSetMenuAlignmentButtonFont(button, font, aligned);
				if (aligned)
					WMGroupButtons(alignments[i][0], button);
				WMPackBoxSubview(alignmentRow, button, WMGetButtonPreferredSize(button), False, 12);
			}
			WMPackBoxSubview(column, alignmentRow, WMGetBoxMinimumSize(alignmentRow), False, 12);
			WMMapSubwidgets(alignmentRow);
		}
		WPrefsCreateIconPositionControl(&positions[i], column, font);
		WPrefsSetIconPositionControlValue(&positions[i], 5);
		size = WMGetViewSize(WMWidgetView(positions[i].box));
		WMPackBoxSubview(column, positions[i].box, size, False, 0);
		WMPackBoxSubview(row, column, WMGetBoxMinimumSize(column), False, i == 2 ? 0 : 30);
		WMMapSubwidgets(column);
		WMReleaseFont(font);
	}
	total = WMGetBoxMinimumSize(row);
	assert(WMGetButtonPreferredSize(speeds[2].buttons[0]).width >
	       WMGetButtonPreferredSize(speeds[0].buttons[0]).width);
	WMResizeWidget(row, total.width, total.height);
	WMResizeWidget(window, total.width, total.height);
	WMRealizeWidget(window);
	WMMapSubwidgets(row);
	WMMapSubwidgets(window);
	WMMapWidget(window);
	XSync(display, False);
	for (i = 0; i < 3; i++) {
		RColor red = { 255, 0, 0, 255 };

		checkSpeed(&speeds[i]);
		checkPositions(&positions[i]);
		WPrefsSetSpeedControlValue(&speeds[i], 3);
		WPrefsSetIconPositionControlValue(&positions[i], 5);
		for (int aligned = 0; aligned < 2; aligned++) {
			WMButton *button = alignments[i][aligned];
			RColor black = { 0, 0, 0, 255 };
			int height;

			WMPerformButtonClick(button);
			assert(WMGetButtonSelected(button));
			assert(!WMGetButtonSelected(alignments[i][!aligned]));
			assert(WMGetButtonPreferredSize(button).width <= WMWidgetWidth(button));
			/* Ignore the button bevel when measuring the menu drawing itself. */
			WMSetButtonBordered(button, False);
			height = symbolHeight(button, black);
			assert(height >= WMWidgetHeight(button) / 2);
			if (i > 0) {
				RImage *image;
				WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
				int side = 32 * WMAX(17, WMFontHeight(font)) / 17;
				int x = (WMWidgetWidth(button) - side) / 2 + side / 4;
				int y = (WMWidgetHeight(button) - side) / 2 + side * (aligned ? 6 : 4) / 32;
				int stride;
				unsigned char *title, *item;

				WMReleaseFont(font);
				image = RCreateImageFromDrawable(WMScreenRContext(screen), WMWidgetXID(button), None);
				assert(image);
				stride = image->format == RRGBAFormat ? 4 : 3;
				title = image->data + stride * (y * image->width + x);
				item = image->data + stride * ((y + side * 5 / 32) * image->width + x);
				assert(title[0] + title[1] + title[2] + 100 < item[0] + item[1] + item[2]);
				RReleaseImage(image);
				WMSetButtonBordered(alignments[0][aligned], False);
				assert(height > symbolHeight(alignments[0][aligned], black));
				WMSetButtonBordered(alignments[0][aligned], True);
			}
			WMSetButtonBordered(button, True);
		}
		for (int clock = 0; clock < 6; clock++) {
			int redHeight;
			WMSize size = WMGetViewSize(WMWidgetView(clocks[i][1][clock]));

			WMSetButtonSelected(clocks[i][1][clock], True);
			WMSetButtonSelected(clocks[i][0][clock], False);
			redHeight = symbolHeight(clocks[i][1][clock], red);
			assert(WMGetButtonPreferredSize(clocks[i][1][clock]).height <= size.height);
			assert(symbolHeight(clocks[i][0][clock], red) == 0);
			if (clock == 0)
				assert(redHeight == 0);
			else
				assert(redHeight >= size.height / 5);
		}
	}
	if (gallery) {
		WMAddTimerHandler(15000, finish, window);
		while (!done) {
			XEvent event;

			WMNextEvent(display, &event);
			WMHandleEvent(&event);
		}
	} else {
		WMFont *normal = WMSystemFontOfSize(screen, 11);
		WMSize baseline = WMGetButtonPreferredSize(speeds[0].buttons[0]);

		WPrefsSetSpeedControlFont(&speeds[2], normal);
		assert(WMGetButtonPreferredSize(speeds[2].buttons[0]).width == baseline.width);
		assert(WPrefsGetSpeedControlValue(&speeds[2]) == 3);
		WPrefsSetIconPositionControlFont(&positions[2], normal);
		assert(positions[2].position == 5);
		assert(WMGetViewSize(WMWidgetView(positions[2].box)).width ==
		       WMGetViewSize(WMWidgetView(positions[0].box)).width);
		checkPositions(&positions[2]);
		checkSpeed(&speeds[2]);
		WPrefsSetClockButtonFont(clocks[2][1][3], normal, 3);
		assert(WMGetButtonSelected(clocks[2][1][3]));
		assert(WMGetButtonPreferredSize(clocks[2][1][3]).width ==
		       WMGetButtonPreferredSize(clocks[0][1][3]).width);
		for (int aligned = 0; aligned < 2; aligned++) {
			Bool selected = WMGetButtonSelected(alignments[2][aligned]);

			WPrefsSetMenuAlignmentButtonFont(alignments[2][aligned], normal, aligned);
			assert(WMGetButtonSelected(alignments[2][aligned]) == selected);
			assert(WMGetButtonPreferredSize(alignments[2][aligned]).width ==
			       WMGetButtonPreferredSize(alignments[0][aligned]).width);
		}
		WMReleaseFont(normal);
	}
	WMDestroyWidget(window);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware WPrefs control tests passed");
	return 0;
}
