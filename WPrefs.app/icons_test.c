#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "Icons.c"

static WMPropList *domain;

char *GetStringForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMGetFromPLDictionary(domain, name);

	WMReleasePropList(name);
	return value ? WMGetFromPLString(value) : NULL;
}

void SetStringForKey(const char *value, const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *text = WMCreatePLString(value);

	WMPutInPLDictionary(domain, name, text);
	WMReleasePropList(name);
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
	(void)name;
	return NULL;
}

static void checkBounds(WMView *view)
{
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		checkBounds(child);
	}
}

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };

	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMWindow *window;
		_Panel *panel;
		WMSize size;

		domain = WMCreatePLDictionary(NULL, NULL);
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		SetStringForKey("trh", "IconPosition");
		SetStringForKey("flip", "IconificationStyle");
		SetIntegerForKey(96, "IconSize");
		SetBoolForKey(True, "MiniwindowPreviewBalloons");
		SetIntegerForKey(137, "MiniPreviewSize");
		window = WMCreateWindow(screen, "iconsTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitIcons(window);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));
		assert(panel->position.position == 3);
		assert(WMGetPopUpButtonSelectedItem(panel->sizeP) == 9);
		assert(WMGetPopUpButtonSelectedItem(panel->animP) == 2);
		assert(WMGetSliderValue(panel->minipreview.slider) == 137);
		for (int j = 0; j < 8; j++) {
			WMPerformButtonClick(panel->position.buttons[j]);
			storeData(panel);
			assert(strcmp(GetStringForKey("IconPosition"), icon_position_dbvalue[j]) == 0);
			checkBounds(WMWidgetView(panel->position.box));
		}
		assert(GetIntegerForKey("MiniPreviewSize") == 136);
		for (int j = 0; j < wlengthof(icon_animation); j++) {
			WMSetPopUpButtonSelectedItem(panel->animP, j);
			storeData(panel);
			assert(strcmp(GetStringForKey("IconificationStyle"), icon_animation[j].db_value) == 0);
		}
		WMSetSliderValue(panel->minipreview.slider, minipreview_minimum_size);
		minipreview_slider_changed(NULL, panel);
		storeData(panel);
		assert(!GetBoolForKey("MiniwindowPreviewBalloons"));
		WMSetSliderValue(panel->minipreview.slider, 512);
		minipreview_slider_changed(NULL, panel);
		storeData(panel);
		assert(GetBoolForKey("MiniwindowPreviewBalloons"));
		assert(GetIntegerForKey("MiniPreviewSize") == 512);
		WMSetPopUpButtonSelectedItem(panel->sizeP, 29);
		WMPerformButtonClick(panel->arrB);
		WMPerformButtonClick(panel->omnB);
		WMPerformButtonClick(panel->sclB);
		WMPerformButtonClick(panel->marginB);
		storeData(panel);
		assert(GetIntegerForKey("IconSize") == 256);
		assert(GetBoolForKey("AutoArrangeIcons") && GetBoolForKey("StickyIcons"));
		assert(GetBoolForKey("SingleClickLaunch") && GetBoolForKey("EnforceIconMargin"));
		{
			int width = WMWidgetWidth(panel->position.screen);
			int height = WMWidgetHeight(panel->position.screen);

			WMResizeWidget(window, size.width + 304, size.height + 204);
			assert(WMWidgetWidth(panel->position.screen) > width);
			assert(WMWidgetHeight(panel->position.screen) > height);
		}
		checkBounds(WMWidgetView(panel->box));
		assert(panel->position.position == 7);
		assert(WMWidgetWidth(panel->position.screen) >
		       WMWidgetWidth(panel->position.marker) + 4);
		for (int j = 0; j < 8; j++) {
			WMPerformButtonClick(panel->position.buttons[j]);
			checkBounds(WMWidgetView(panel->position.box));
		}
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
		puts("SKIP: icons_test needs an X server");
		return 77;
	}
	WMInitializeApplication("icons_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware icon preferences tests passed");
	return 0;
}
