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
	WMDestroyWidget(window);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware layout tests passed");
	return 0;
}
