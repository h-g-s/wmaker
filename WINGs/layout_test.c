#include "WINGsP.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <X11/keysym.h>

static void testFontSizes(WMWindow *window)
{
	WMButton *button = WMCreateCommandButton(window);
	WMButton *toggle = WMCreateSwitchButton(window);
	WMLabel *label = WMCreateLabel(window);
	WMTextField *field = WMCreateTextField(window);
	WMSize previous = { 0, 0 };
	const char *text = "A longer translated button caption";
	int sizes[] = { 12, 18, 24, 32 };
	unsigned int i;

	WMSetButtonText(button, text);
	WMSetButtonText(toggle, text);
	WMSetLabelText(label, text);
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		WMFont *font = WMSystemFontOfSize(WMWidgetScreen(window), sizes[i]);
		WMSize size;
		int width, height;

		assert(font);
		width = WMWidthOfString(font, text, strlen(text));
		height = WMFontHeight(font);
		WMSetButtonFont(button, font);
		WMSetButtonFont(toggle, font);
		WMSetLabelFont(label, font);
		WMSetTextFieldFont(field, font);
		size = WMGetButtonPreferredSize(button);
		assert(size.width > width && size.height > height);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		size = WMGetButtonPreferredSize(toggle);
		assert(size.width >= width + W_VIEW(window)->screen->checkButtonImageOff->width + 5);
		assert(size.height >= W_VIEW(window)->screen->checkButtonImageOff->height);
		assert(size.height > height);
		size = WMGetLabelPreferredSize(label);
		assert(size.width >= width && size.height >= height);
		size = WMGetTextFieldPreferredSize(field, 26);
		assert(size.width > 26 * WMWidthOfString(font, "M", 1));
		assert(size.height > height);
		WMSetTextFieldText(field, "/a/very/long/command/that/should/scroll/not/widen/the/form");
		assert(size.width == WMGetTextFieldPreferredSize(field, 26).width);
		WMReleaseFont(font);
	}

	WMSetButtonAltText(button, "An even longer alternate caption that must also fit");
	assert(WMGetButtonPreferredSize(button).width > previous.width);
}

static void testLabelContent(WMWindow *window)
{
	WMLabel *label = WMCreateLabel(window);
	WMFont *font = WMDefaultSystemFont(WMWidgetScreen(window));
	WMPixmap *image = W_VIEW(window)->screen->checkButtonImageOff;
	WMSize size;

	WMSetLabelText(label, "First\nA longer second line");
	size = WMGetLabelPreferredSize(label);
	assert(size.width >= WMWidthOfString(font, "A longer second line", 20));
	assert(size.height >= 2 * WMFontHeight(font));
	WMSetLabelImage(label, image);
	WMSetLabelImagePosition(label, WIPLeft);
	assert(WMGetLabelPreferredSize(label).width >= size.width + image->width + 5);
	WMSetLabelImagePosition(label, WIPImageOnly);
	size = WMGetLabelPreferredSize(label);
	assert(size.width == image->width && size.height == image->height);
	WMReleaseFont(font);
}

static void testNestedBoxes(WMWindow *window)
{
	WMBox *column = WMCreateBox(window);
	WMBox *row = WMCreateBox(column);
	WMButton *first = WMCreateCommandButton(row);
	WMButton *second = WMCreateCommandButton(row);
	WMSize size, after;
	WMPoint firstPosition, secondPosition;
	WMBox *small = WMCreateBox(window);
	WMButton *expanding = WMCreateCommandButton(small);

	WMPackBoxSubview(small, expanding, wmksize(80, 60), True, 0);
	assert(WMWidgetHeight(expanding) == 60);
	WMResizeWidget(small, 100, 100);
	assert(WMWidgetHeight(expanding) == 100);

	WMSetBoxHorizontal(row, True);
	WMSetBoxBorderWidth(row, 5);
	WMResizeWidget(second, 180, 45);
	WMPackBoxSubview(row, first, wmksize(120, 35), True, 7);
	WMAddBoxSubviewAtEnd(row, WMWidgetView(second), False, True, 180, 0, 0);
	size = WMGetBoxMinimumSize(row);
	assert(size.width == 317 && size.height == 55);
	WMResizeWidget(row, size.width, size.height);
	WMSetBoxBorderWidth(column, 10);
	WMAddBoxSubview(column, WMWidgetView(row), False, True, size.height, 0, 0);
	size = WMGetBoxMinimumSize(column);
	assert(size.width == 337 && size.height == 75);
	WMResizeWidget(column, size.width + 100, size.height);
	after = WMGetBoxMinimumSize(column);
	assert(after.width == size.width && after.height == size.height);
	after = WMGetBoxMinimumSize(row);
	assert(after.width == 317 && after.height == 55);
	firstPosition = WMGetViewPosition(WMWidgetView(first));
	secondPosition = WMGetViewPosition(WMWidgetView(second));
	assert(firstPosition.x + (int)WMWidgetWidth(first) + 7 <= secondPosition.x);
	assert(WMWidgetHeight(first) >= 35 && WMWidgetHeight(second) >= 45);
}

static void testWrappedLabel(WMWindow *window)
{
	WMLabel *label = WMCreateLabel(window);
	WMFont *font = WMDefaultSystemFont(WMWidgetScreen(window));
	WMSize narrow, wide;

	WMSetLabelText(label, "A long translated message that needs several lines rather than a fixed text area.");
	WMSetLabelWraps(label, True);
	wide = WMGetLabelPreferredSizeForWidth(label, 300);
	narrow = WMGetLabelPreferredSizeForWidth(label, 100);
	assert(narrow.height > wide.height);
	assert(narrow.height > 2 * WMFontHeight(font));
	WMSetLabelText(label, "\303\251\303\251\303\251");
	narrow = WMGetLabelPreferredSizeForWidth(label, 1);
	assert(narrow.height == 3 * WMFontHeight(font));
	WMReleaseFont(font);
}

static void assertBefore(WMWidget *above, WMWidget *below)
{
	WMPoint first = WMGetViewScreenPosition(WMWidgetView(above));
	WMPoint second = WMGetViewScreenPosition(WMWidgetView(below));

	assert(first.y + (int)WMWidgetHeight(above) <= second.y);
}

typedef struct {
	WMInputPanel *panel;
	KeySym key;
} InputKey;

static void sendInputKey(void *data)
{
	InputKey *input = data;
	Display *display = WMScreenDisplay(WMWidgetScreen(input->panel->win));
	XEvent event = { 0 };

	event.xkey.type = KeyPress;
	event.xkey.display = display;
	event.xkey.window = WMWidgetXID(input->panel->text);
	event.xkey.keycode = XKeysymToKeycode(display, input->key);
	event.xkey.same_screen = True;
	XSendEvent(display, event.xkey.window, False, KeyPressMask, &event);
	XFlush(display);
}

static void testInputKey(WMInputPanel *panel, KeySym key, int result)
{
	InputKey input = { panel, key };

	WMMapWidget(panel->win);
	WMAddTimerHandler(10, sendInputKey, &input);
	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));
	assert(panel->result == result);
	WMUnmapWidget(panel->win);
}

static void testInputPanels(WMScreen *screen)
{
	WMInputPanel *panel;
	WMSize size;
	char *text;

	panel = WMCreateScaledInputPanel(screen, NULL, "A long translated input dialog title",
					"A message with enough words to require more than two lines of text. "
					"The input field and action buttons must remain below the whole message "
					"instead of overlapping it when the font changes.",
					"initial value", "Accept this value", "Cancel without saving");
	assertBefore(panel->tLbl, panel->mLbl);
	assertBefore(panel->mLbl, panel->text);
	assertBefore(panel->text, panel->defBtn);
	size = WMGetButtonPreferredSize(panel->altBtn);
	assert(WMWidgetWidth(panel->altBtn) >= size.width);
	assert(WMWidgetHeight(panel->altBtn) >= size.height);
	size = WMGetTextFieldPreferredSize(panel->text, 28);
	assert(WMWidgetHeight(panel->text) >= size.height);
	text = WMGetTextFieldText(panel->text);
	assert(strcmp(text, "initial value") == 0);
	wfree(text);
	testInputKey(panel, XK_Return, WAPRDefault);
	testInputKey(panel, XK_Escape, WAPRAlternate);
	WMDestroyInputPanel(panel);

	panel = WMCreateScaledInputPanel(screen, NULL, NULL, NULL, "", NULL, "Close");
	assert(panel->tLbl == NULL && panel->mLbl == NULL && panel->defBtn == NULL);
	assertBefore(panel->text, panel->altBtn);
	WMDestroyInputPanel(panel);
}

static void testContentOverflow(WMScreen *screen)
{
	WMWindow *window = WMCreateWindow(screen, "overflow");
	WMBox *box = WMCreateBox(window);
	WMLabel *label = WMCreateLabel(box);
	WMSize size;
	WMPoint position;

	WMSetLabelText(label, "Content must keep its measured size inside the viewport");
	WMPackBoxSubview(box, label, wmksize(600, 400), False, 0);
	size = WMGetBoxMinimumSize(box);
	WMResizeWidget(box, size.width, size.height);
	size = WMResizeWindowToFitContent(window, box, wmksize(300, 200));
	assert(size.width == 300 && size.height == 200);
	assert(WMWidgetWidth(box) == 600 && WMWidgetHeight(box) == 400);
	WMRealizeWidget(window);
	WMMapSubwidgets(box);
	position = WMGetViewScreenPosition(WMWidgetView(box));
	assert(position.x >= 0 && position.y >= 0);
	WMDestroyWidget(window);
}

static void countReturn(XEvent *event, void *data)
{
	int *count = data;

	if (event->type == KeyPress && XLookupKeysym(&event->xkey, 0) == XK_Return)
		(*count)++;
}

static void testNestedListKeys(WMScreen *screen)
{
	WMWindow *window = WMCreateWindow(screen, "listKeys");
	WMBox *box = WMCreateBox(window);
	WMList *list = WMCreateList(box);
	XEvent leave = { 0 }, key = { 0 };
	int windowKeys = 0, responderKeys = 0;
	int i;

	WMAddListItem(list, "An icon");
	WMCreateEventHandler(WMWidgetView(window), KeyPressMask, countReturn, &windowKeys);
	WMCreateEventHandler(WMWidgetView(box), KeyPressMask, countReturn, &responderKeys);
	WMRealizeWidget(window);
	WMMapSubwidgets(box);
	WMMapSubwidgets(window);
	WMMapWidget(window);
	leave.xcrossing.type = LeaveNotify;
	leave.xcrossing.display = WMScreenDisplay(screen);
	leave.xcrossing.window = WMWidgetXID(list);
	key.xkey.type = KeyPress;
	key.xkey.display = WMScreenDisplay(screen);
	key.xkey.window = WMWidgetXID(list);
	key.xkey.keycode = XKeysymToKeycode(key.xkey.display, XK_Return);
	for (i = 0; i < 2; i++) {
		if (i)
			WMSetViewNextResponder(WMWidgetView(list), WMWidgetView(box));
		WMSetFocusToWidget(list);
		WMHandleEvent(&leave);
		WMHandleEvent(&key);
		assert(windowKeys == 1 && responderKeys == i);
	}
	WMDestroyWidget(window);
}

static Bool samePixmapPixels(WMPixmap *first, WMPixmap *second)
{
	Display *display = first->screen->display;
	XImage *a, *b;
	Bool same = True;
	int x, y;

	assert(first->width == second->width && first->height == second->height);
	a = XGetImage(display, first->pixmap, 0, 0, first->width, first->height, AllPlanes, ZPixmap);
	b = XGetImage(display, second->pixmap, 0, 0, second->width, second->height, AllPlanes, ZPixmap);
	assert(a && b);
	for (y = 0; y < first->height; y++)
		for (x = 0; x < first->width; x++)
			if (XGetPixel(a, x, y) != XGetPixel(b, x, y))
				same = False;
	XDestroyImage(a);
	XDestroyImage(b);
	return same;
}

static int interiorWhitePixels(WMPixmap *pixmap)
{
	RImage *image = RCreateImageFromDrawable(pixmap->screen->rcontext, pixmap->pixmap, None);
	int x, y, count = 0;
	int stride;

	assert(image);
	stride = image->format == RRGBAFormat ? 4 : 3;
	for (y = image->height / 5; y < 4 * image->height / 5; y++)
		for (x = image->width / 5; x < 4 * image->width / 5; x++) {
			unsigned char *pixel = image->data + stride * (y * image->width + x);

			if (pixel[0] > 240 && pixel[1] > 240 && pixel[2] > 240)
				count++;
		}
	RReleaseImage(image);
	return count;
}

static void checkSquareLighting(WMPixmap *pixmap, Bool raised)
{
	RImage *image = RCreateImageFromDrawable(pixmap->screen->rcontext, pixmap->pixmap, None);
	long top = 0, bottom = 0, left = 0, right = 0;
	int x, y, stride;

	assert(image);
	stride = image->format == RRGBAFormat ? 4 : 3;
	for (y = 0; y < image->height / 5; y++)
		for (x = image->width / 3; x < 2 * image->width / 3; x++) {
			unsigned char *a = image->data + stride * (y * image->width + x);
			unsigned char *b = image->data + stride * ((image->height - y - 1) * image->width + x);

			top += a[0] + a[1] + a[2];
			bottom += b[0] + b[1] + b[2];
		}
	for (x = 0; x < image->width / 5; x++)
		for (y = image->height / 3; y < 2 * image->height / 3; y++) {
			unsigned char *a = image->data + stride * (y * image->width + x);
			unsigned char *b = image->data + stride * (y * image->width + image->width - x - 1);

			left += a[0] + a[1] + a[2];
			right += b[0] + b[1] + b[2];
		}
	assert(raised ? top > bottom : top < bottom);
	assert(raised ? left > right : left < right);
	RReleaseImage(image);
}

static void testSymbolImages(WMScreen *screen)
{
	WMPixmap *sources[] = {
		screen->checkButtonImageOff, screen->checkButtonImageOn,
		screen->radioButtonImageOff, screen->radioButtonImageOn,
		screen->tristateButtonImageOff, screen->tristateButtonImageOn, screen->tristateButtonImageTri,
		screen->buttonArrow, screen->pushedButtonArrow, screen->popUpIndicator, screen->pullDownIndicator
	};
	WMFont *small = WMSystemFontOfSize(screen, 1);
	WMFont *large = WMSystemFontOfSize(screen, 24);
	WMFont *larger = WMSystemFontOfSize(screen, 32);
	unsigned int i;

	for (i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
		WMPixmap *base = W_CreateButtonImage(screen, sources[i], small);
		WMPixmap *scaled = W_CreateButtonImage(screen, sources[i], large);
		WMPixmap *bigger = W_CreateButtonImage(screen, sources[i], larger);

		assert(base == sources[i]);
		assert(scaled->width > base->width && scaled->height > base->height);
		assert(bigger->width > scaled->width && bigger->height > scaled->height);
		if (i <= 1 || (i >= 4 && i <= 6) || i == 9) {
			Bool raised = i <= 1 || i == 9;

			checkSquareLighting(base, raised);
			checkSquareLighting(scaled, raised);
			checkSquareLighting(bigger, raised);
		}
		if (i == 1 || i == 3) {
			assert(interiorWhitePixels(scaled) > scaled->height / 2);
			assert(interiorWhitePixels(bigger) > interiorWhitePixels(scaled));
		}
		if (i == 6) {
			WMPixmap *patterns[] = { scaled, bigger };
			unsigned int j;

			for (j = 0; j < sizeof(patterns) / sizeof(patterns[0]); j++) {
				WMPixmap *pattern = patterns[j];
				RImage *image = RCreateImageFromDrawable(screen->rcontext, pattern->pixmap, None);
				int x, y, left = image ? image->width : 0, right = 0;
				int top = image ? image->height : 0, bottom = 0;
				int stride;

				assert(image);
				stride = image->format == RRGBAFormat ? 4 : 3;
				for (y = image->height / 6; y < 5 * image->height / 6; y++)
					for (x = image->width / 6; x < 5 * image->width / 6; x++) {
						unsigned char *pixel = image->data + stride * (y * image->width + x);

						if (pixel[0] < 60 && pixel[1] < 60 && pixel[2] < 60) {
							left = WMIN(left, x);
							right = WMAX(right, x);
							top = WMIN(top, y);
							bottom = WMAX(bottom, y);
						}
					}
				assert(right - left >= image->width / 2);
				assert(bottom - top >= image->height / 2);
				RReleaseImage(image);
			}
		}
		if (i == 0 || i == 2 || i == 4 || i == 7) {
			WMPixmap *alternate = W_CreateButtonImage(screen, sources[i + 1], large);

			assert(!samePixmapPixels(scaled, alternate));
			WMReleasePixmap(alternate);
		}
		WMReleasePixmap(base);
		WMReleasePixmap(scaled);
		WMReleasePixmap(bigger);
	}
	WMReleaseFont(small);
	WMReleaseFont(large);
	WMReleaseFont(larger);
}

static void testSymbolWidgets(WMWindow *window)
{
	WMScreen *screen = WMWidgetScreen(window);
	WMButton *button = WMCreateSwitchButton(window);
	WMPopUpButton *popup = WMCreatePopUpButton(window);
	WMPixmap *custom = WMCreatePixmap(screen, 20, 10, screen->depth, False);
	WMFont *normal = WMSystemFontOfSize(screen, 12);
	WMFont *large = WMSystemFontOfSize(screen, 32);
	WMSize small, big;

	WMSetButtonText(button, NULL);
	WMSetButtonImagePosition(button, WIPImageOnly);
	WMSetButtonFont(button, normal);
	small = WMGetButtonPreferredSize(button);
	WMSetButtonFont(button, large);
	big = WMGetButtonPreferredSize(button);
	assert(big.width > small.width && big.height > small.height);
	WMSetButtonFont(button, normal);
	assert(WMGetButtonPreferredSize(button).width == small.width);
	WMSetButtonImageDefault(button);
	WMSetButtonFont(button, large);
	big = WMGetButtonPreferredSize(button);
	assert(big.width > screen->buttonArrow->width + 4);
	WMSetButtonAltImage(button, NULL);
	WMSetButtonImage(button, custom);
	assert(WMGetButtonPreferredSize(button).width == 24);
	WMSetButtonFont(button, normal);
	assert(WMGetButtonPreferredSize(button).width == 24);

	WMAddPopUpButtonItem(popup, "A longer choice");
	WMAddPopUpButtonItem(popup, "Short");
	WMSetPopUpButtonSelectedItem(popup, 1);
	WMSetPopUpButtonFont(popup, normal);
	small = WMGetPopUpButtonPreferredSize(popup);
	WMSetPopUpButtonFont(popup, large);
	big = WMGetPopUpButtonPreferredSize(popup);
	assert(big.width > small.width && big.height > small.height);
	assert(big.width > WMWidthOfString(large, "A longer choice", 15) + screen->popUpIndicator->width);
	assert(WMGetPopUpButtonSelectedItem(popup) == 1);
	WMSetPopUpButtonPullsDown(popup, True);
	big = WMGetPopUpButtonPreferredSize(popup);
	assert(big.height > small.height);
	WMSetPopUpButtonFont(popup, normal);
	WMSetPopUpButtonPullsDown(popup, False);
	assert(WMGetPopUpButtonPreferredSize(popup).width == small.width);

	WMReleaseFont(normal);
	WMReleaseFont(large);
	WMReleasePixmap(custom);
}

static void testButtonBorders(WMScreen *screen)
{
	WMFont *small = WMSystemFontOfSize(screen, 1);
	WMFont *large = WMSystemFontOfSize(screen, 32);
	WMPixmap *old = WMCreatePixmap(screen, 100, 80, screen->depth, False);
	WMPixmap *scaled = WMCreatePixmap(screen, 100, 80, screen->depth, False);
	WMReliefType reliefs[] = { WRRaised, WRSunken, WRPushed };
	unsigned int i;

	for (i = 0; i < sizeof(reliefs) / sizeof(reliefs[0]); i++) {
		XImage *image;

		XFillRectangle(screen->display, old->pixmap, WMColorGC(screen->gray), 0, 0, 100, 80);
		XFillRectangle(screen->display, scaled->pixmap, WMColorGC(screen->gray), 0, 0, 100, 80);
		W_DrawRelief(screen, old->pixmap, 0, 0, 100, 80, reliefs[i]);
		W_DrawButtonRelief(screen, scaled->pixmap, 0, 0, 100, 80, reliefs[i], small);
		assert(samePixmapPixels(old, scaled));
		W_DrawButtonRelief(screen, scaled->pixmap, 0, 0, 100, 80, reliefs[i], large);
		assert(!samePixmapPixels(old, scaled));
		image = XGetImage(screen->display, scaled->pixmap, 0, 0, 100, 80, AllPlanes, ZPixmap);
		assert(image);
		assert(XGetPixel(image, 50, 1) == W_PIXEL(reliefs[i] == WRRaised ? screen->white :
						       reliefs[i] == WRSunken ? screen->darkGray : screen->black));
		assert(XGetPixel(image, 50, 78) == W_PIXEL(reliefs[i] == WRRaised ? screen->black : screen->white));
		XDestroyImage(image);
	}
	WMReleasePixmap(old);
	WMReleasePixmap(scaled);
	WMReleaseFont(small);
	WMReleaseFont(large);
}

static void testSliders(WMScreen *screen)
{
	WMWindow *window = WMCreateWindow(screen, "sliders");
	WMSlider *slider = WMCreateSlider(window);
	WMSlider *original = WMCreateSlider(window);
	WMFont *normal = WMSystemFontOfSize(screen, 1);
	WMFont *large = WMSystemFontOfSize(screen, 32);
	WMSize small = WMGetSliderPreferredSize(slider), big;
	XEvent event = { 0 };
	XImage *image, *baseline;
	int x, y, orientation;

	WMResizeWidget(window, 500, 300);
	WMMoveWidget(original, 0, 200);
	WMRealizeWidget(window);
	WMMapSubwidgets(window);
	WMMapWidget(window);
	WMSetSliderFont(slider, normal);
	WMSetSliderValue(slider, 50);
	WMSetSliderValue(original, 50);
	XSync(screen->display, False);
	image = XGetImage(screen->display, WMWidgetXID(slider), 0, 0, 100, 16, AllPlanes, ZPixmap);
	baseline = XGetImage(screen->display, WMWidgetXID(original), 0, 0, 100, 16, AllPlanes, ZPixmap);
	assert(image && baseline);
	for (y = 0; y < 16; y++)
		for (x = 0; x < 100; x++)
			assert(XGetPixel(image, x, y) == XGetPixel(baseline, x, y));
	XDestroyImage(image);
	XDestroyImage(baseline);
	WMSetSliderFont(slider, large);
	big = WMGetSliderPreferredSize(slider);
	assert(big.width > small.width && big.height > small.height);
	WMResizeWidget(window, big.width + 20, big.height + 250);
	WMResizeWidget(slider, big.width, big.height);
	WMSetSliderValue(slider, 50);
	/* Resizing in the same orientation must regenerate the knob's pixmap too. */
	WMResizeWidget(slider, big.width, big.height + 20);
	WMSetSliderValue(slider, 50);
	XSync(screen->display, False);
	image = XGetImage(screen->display, WMWidgetXID(slider), 0, 0,
			  big.width, big.height + 20, AllPlanes, ZPixmap);
	assert(image);
	assert(XGetPixel(image, big.width / 2 - 10, big.height - 2) == W_PIXEL(screen->gray));
	XDestroyImage(image);
	event.xbutton.display = screen->display;
	event.xbutton.window = WMWidgetXID(slider);
	event.xbutton.button = Button2;
	for (orientation = 0; orientation < 2; orientation++) {
		WMResizeWidget(slider, orientation ? big.height : big.width,
			       orientation ? big.width : big.height);
		for (x = 0; x < 2; x++) {
			event.type = ButtonPress;
			event.xbutton.x = orientation ? big.height / 2 : (x ? big.width - 1 : 0);
			event.xbutton.y = orientation ? (x ? big.width - 1 : 0) : big.height / 2;
			WMHandleEvent(&event);
			assert(WMGetSliderValue(slider) == (x ? 100 : 0));
			event.type = ButtonRelease;
			WMHandleEvent(&event);
		}
	}
	WMResizeWidget(slider, 100, 16);
	WMSetSliderFont(slider, normal);
	assert(WMGetSliderPreferredSize(slider).width == small.width);
	WMSetSliderKnobThickness(slider, 8);
	WMSetSliderFont(slider, large);
	WMResizeWidget(slider, 4, 4);
	WMSetSliderValue(slider, 50);
	WMReleaseFont(normal);
	WMReleaseFont(large);
	WMDestroyWidget(window);
}

int main(int argc, char **argv)
{
	Display *display;
	WMScreen *screen;
	WMWindow *window;

	if (!getenv("DISPLAY")) {
		puts("SKIP: layout_test needs an X server");
		return 77;
	}
	WMInitializeApplication("layout_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	window = WMCreateWindow(screen, "layout");
	testFontSizes(window);
	testLabelContent(window);
	testNestedBoxes(window);
	testWrappedLabel(window);
	testInputPanels(screen);
	testContentOverflow(screen);
	testNestedListKeys(screen);
	testSymbolImages(screen);
	testSymbolWidgets(window);
	testButtonBorders(screen);
	testSliders(screen);
	WMDestroyWidget(window);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware layout tests passed");
	return 0;
}
