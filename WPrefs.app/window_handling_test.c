#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "WindowHandling.c"

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

char *GetStringForKey(const char *key)
{
	WMPropList *value = GetObjectForKey(key);

	return value ? WMGetFromPLString(value) : NULL;
}

void SetStringForKey(const char *value, const char *key)
{
	WMPropList *text = WMCreatePLString(value);

	SetObjectForKey(text, key);
	WMReleasePropList(text);
}

int GetIntegerForKey(const char *key)
{
	char *value = GetStringForKey(key);

	return value ? atoi(value) : 0;
}

void SetIntegerForKey(int value, const char *key)
{
	char text[32];

	sprintf(text, "%d", value);
	SetStringForKey(text, key);
}

Bool GetBoolForKey(const char *key)
{
	char *value = GetStringForKey(key);

	return value && strcmp(value, "YES") == 0;
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
		if (child->self && WMWidgetClass(child->self) == WC_Button)
			assert(WMGetButtonPreferredSizeForWidth(child->self, child->size.width).height
			       <= child->size.height);
		checkBounds(child);
	}
}

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	int previousMarker = 0, previousPreview = 0;

	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMWindow *window;
		_Panel *panel;
		WMSize size;
		WMPropList *origin;

		domain = WMCreatePLDictionary(NULL, NULL);
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		SetStringForKey("smart", "WindowPlacement");
		SetStringForKey("RestoreGeometry", "DragMaximizedWindow");
		SetIntegerForKey(37, "EdgeResistance");
		SetIntegerForKey(13, "ResizeIncrement");
		SetBoolForKey(True, "OpaqueResize");
		SetBoolForKey(True, "Attraction");
		window = WMCreateWindow(screen, "handlingTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitWindowHandling(window);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));
		assert(WMWidgetWidth(panel->porigW) > previousMarker);
		assert(WMWidgetWidth(panel->opaqB) > previousPreview);
		previousMarker = WMWidgetWidth(panel->porigW);
		previousPreview = WMWidgetWidth(panel->opaqB);
		assert(WMGetPopUpButtonSelectedItem(panel->placP) == 4);
		assert(WMGetPopUpButtonSelectedItem(panel->dragmaxP) == 1);
		assert(WMGetSliderValue(panel->resS) == 37);
		assert(WMGetSliderValue(panel->resizeS) == 13);
		assert(WMGetButtonSelected(panel->opaqresizeB));
		assert(WMGetButtonSelected(panel->resrB));
		for (int edge = 0; edge < 2; edge++) {
			WMSetSliderValue(panel->hsli, edge ? WMGetSliderMaxValue(panel->hsli) : 0);
			WMSetSliderValue(panel->vsli, edge ? WMGetSliderMaxValue(panel->vsli) : 0);
			sliderCallback(NULL, panel);
			checkBounds(WMWidgetView(panel->box));
			if (edge) {
				WMPoint position = WMGetViewPosition(WMWidgetView(panel->porigW));

				assert(position.x + WMWidgetWidth(panel->porigW) == WMWidgetWidth(panel->porigF) - 2);
				assert(position.y + WMWidgetHeight(panel->porigW) == WMWidgetHeight(panel->porigF) - 2);
			}
		}
		storeData(panel);
		origin = GetObjectForKey("WindowPlaceOrigin");
		assert(atoi(WMGetFromPLString(WMGetFromPLArray(origin, 0))) == WMGetSliderMaxValue(panel->hsli));
		assert(atoi(WMGetFromPLString(WMGetFromPLArray(origin, 1))) == WMGetSliderMaxValue(panel->vsli));
		for (int j = 0; j < wlengthof(window_placements); j++) {
			WMSetPopUpButtonSelectedItem(panel->placP, j);
			storeData(panel);
			assert(strcmp(GetStringForKey("WindowPlacement"), window_placements[j].db_value) == 0);
		}
		for (int j = 0; j < wlengthof(drag_maximized_window_options); j++) {
			WMSetPopUpButtonSelectedItem(panel->dragmaxP, j);
			storeData(panel);
			assert(strcmp(GetStringForKey("DragMaximizedWindow"), drag_maximized_window_options[j].db_value) == 0);
		}
		WMSetSliderValue(panel->resS, 0);
		WMSetSliderValue(panel->resizeS, 100);
		resistanceCallback(NULL, panel);
		resizeCallback(NULL, panel);
		WMPerformButtonClick(panel->resaB);
		WMPerformButtonClick(panel->opaqB);
		WMPerformButtonClick(panel->opaqkeybB);
		WMPerformButtonClick(panel->miconB);
		WMPerformButtonClick(panel->mdockB);
		storeData(panel);
		assert(GetIntegerForKey("EdgeResistance") == 0 && GetIntegerForKey("ResizeIncrement") == 100);
		assert(!GetBoolForKey("Attraction"));
		assert(GetBoolForKey("OpaqueMove") && GetBoolForKey("OpaqueMoveResizeKeyboard"));
		assert(GetBoolForKey("NoWindowOverIcons") && GetBoolForKey("NoWindowOverDock"));
		WMSetSliderValue(panel->resS, 80);
		undo(panel);
		assert(WMGetSliderValue(panel->resS) == 0);
		checkBounds(WMWidgetView(panel->box));
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
		puts("SKIP: window_handling_test needs an X server");
		return 77;
	}
	WMInitializeApplication("window_handling_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware window handling tests passed");
	return 0;
}
