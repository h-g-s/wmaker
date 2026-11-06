#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "HotCornerShortcuts.c"

static WMPropList *domain;

WMPropList *GetObjectForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMGetFromPLDictionary(domain, name);

	WMReleasePropList(name);
	return value;
}

void SetObjectForKey(WMPropList *value, const char *key)
{
	WMPropList *name = WMCreatePLString(key);

	WMPutInPLDictionary(domain, name, value);
	WMReleasePropList(name);
}

int GetIntegerForKey(const char *key)
{
	WMPropList *value = GetObjectForKey(key);

	return value ? atoi(WMGetFromPLString(value)) : 0;
}

void SetIntegerForKey(int value, const char *key)
{
	char text[32];
	WMPropList *item;

	snprintf(text, sizeof(text), "%d", value);
	item = WMCreatePLString(text);
	SetObjectForKey(item, key);
	WMReleasePropList(item);
}

Bool GetBoolForKey(const char *key)
{
	return GetIntegerForKey(key) != 0;
}

void SetBoolForKey(Bool value, const char *key)
{
	SetIntegerForKey(value, key);
}

void AddSection(Panel *panel, const char *iconFile)
{
	(void)panel;
	(void)iconFile;
}

char *LocateImage(const char *name)
{
	char *path = wmalloc(strlen(TEST_IMAGE_PATH) + strlen(name) + 7);

	sprintf(path, "%s/%s.tiff", TEST_IMAGE_PATH, name);
	return path;
}

static void checkBounds(WMView *view)
{
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		if (child->self && WMWidgetClass(child->self) == WC_Button) {
			WMSize size = WMGetButtonPreferredSize(child->self);

			assert(size.width <= child->size.width && size.height <= child->size.height);
		}
		checkBounds(child);
	}
}

static void checkCorner(_Panel *panel, int corner)
{
	WMPixmap *pixmap = WMGetLabelImage(panel->icornerL[corner]);
	RImage *image;
	int pixels = 0;
	long xsum = 0, ysum = 0;

	assert(pixmap);
	image = RCreateImageFromDrawable(pixmap->screen->rcontext, pixmap->pixmap, None);
	assert(image);
	for (int y = 0; y < image->height; y++)
		for (int x = 0; x < image->width; x++) {
			unsigned char *pixel = image->data + 3 * (y * image->width + x);

			if (pixel[0] > 200 && pixel[1] > 200 && pixel[2] > 200) {
				xsum += x;
				ysum += y;
				pixels++;
			}
		}
	assert(pixels > 0);
	assert((xsum * 2 > pixels * image->width) == (corner % 2 != 0));
	assert((ysum * 2 > pixels * image->height) == (corner / 2 != 0));
	RReleaseImage(image);
}

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	int previousImageWidth = 0;

	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMWindow *window;
		_Panel *panel;
		WMPropList *array;
		WMSize size;
		char *text;

		domain = WMCreatePLDictionary(NULL, NULL);
		SetIntegerForKey(3, "HotCornerEdge");
		SetBoolForKey(True, "HotCorners");
		array = WMCreatePropListFromDescription("(one, None, three, four)");
		assert(array);
		SetObjectForKey(array, "HotCornerActions");
		WMReleasePropList(array);
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		window = WMCreateWindow(screen, "hotCornerTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitHotCornerShortcuts(window);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));
		assert(WMGetButtonSelected(panel->hcB));
		assert(WMGetButtonSelected(panel->hcdelayB[1]));
		for (int corner = 0; corner < 4; corner++)
			checkCorner(panel, corner);
		assert(WMGetLabelImage(panel->icornerL[0])->width > previousImageWidth);
		previousImageWidth = WMGetLabelImage(panel->icornerL[0])->width;
		for (int preset = 0; preset < 5; preset++) {
			WMPerformButtonClick(panel->hcdelayB[preset]);
			storeData(panel);
			assert(GetIntegerForKey("HotCornerDelay") == delayValues[preset]);
			showData(panel);
			assert(WMGetButtonSelected(panel->hcdelayB[preset]));
		}
		WMSetTextFieldText(panel->hcdelayT, "137");
		delayTextChanged(panel, NULL);
		for (int preset = 0; preset < 5; preset++)
			assert(!WMGetButtonSelected(panel->hcdelayB[preset]));
		storeData(panel);
		showData(panel);
		text = WMGetTextFieldText(panel->hcdelayT);
		assert(strcmp(text, "137") == 0);
		wfree(text);
		for (int preset = 0; preset < 5; preset++)
			assert(!WMGetButtonSelected(panel->hcdelayB[preset]));
		for (int edge = 2; edge <= 10; edge++) {
			WMSize label;

			WMSetSliderValue(panel->hceS, edge);
			edgeCallback(NULL, panel);
			label = WMGetLabelPreferredSize(panel->hceL);
			assert(label.width <= WMWidgetWidth(panel->hceL));
			storeData(panel);
			assert(GetIntegerForKey("HotCornerEdge") == edge);
		}
		WMSetButtonSelected(panel->hcB, False);
		WMSetTextFieldText(panel->hcactionsT[0], "wmaker-command --argument");
		WMSetTextFieldText(panel->hcactionsT[2], NULL);
		storeData(panel);
		assert(!GetBoolForKey("HotCorners"));
		array = GetObjectForKey("HotCornerActions");
		assert(WMGetPropListItemCount(array) == 4);
		assert(strcmp(WMGetFromPLString(WMGetFromPLArray(array, 0)), "wmaker-command --argument") == 0);
		assert(strcmp(WMGetFromPLString(WMGetFromPLArray(array, 1)), "None") == 0);
		assert(strcmp(WMGetFromPLString(WMGetFromPLArray(array, 2)), "None") == 0);
		assert(strcmp(WMGetFromPLString(WMGetFromPLArray(array, 3)), "four") == 0);
		showData(panel);
		text = WMGetTextFieldText(panel->hcactionsT[2]);
		assert(!*text);
		wfree(text);
		array = WMCreatePropListFromDescription("(one, (invalid), three, four)");
		assert(array);
		SetObjectForKey(array, "HotCornerActions");
		WMReleasePropList(array);
		showData(panel);
		for (int corner = 0; corner < 4; corner++) {
			text = WMGetTextFieldText(panel->hcactionsT[corner]);
			assert(!*text);
			wfree(text);
		}
		{
			int width = WMWidgetWidth(panel->hcactionsT[0]);

			WMResizeWidget(window, size.width + 204, size.height + 204);
			assert(WMWidgetWidth(panel->hcactionsT[0]) > width);
			checkBounds(WMWidgetView(panel->box));
			WMResizeWidget(window, size.width + 4, size.height + 4);
			checkBounds(WMWidgetView(panel->box));
		}
		WMRemoveNotificationObserver(panel);
		WMDestroyWidget(window);
		wfree(panel);
		WMReleasePropList(domain);
	}
}

int main(int argc, char **argv)
{
	Display *display;
	WMScreen *screen;

	if (!getenv("DISPLAY")) {
		puts("SKIP: hot_corner_test needs an X server");
		return 77;
	}
	WMInitializeApplication("hot_corner_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware hot corner tests passed");
	return 0;
}
