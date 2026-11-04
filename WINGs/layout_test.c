#include "WINGsP.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

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
	WMResizeWidget(first, 120, 35);
	WMResizeWidget(second, 180, 45);
	WMAddBoxSubview(row, WMWidgetView(first), True, True, 120, 0, 7);
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
	WMDestroyWidget(window);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware layout tests passed");
	return 0;
}
