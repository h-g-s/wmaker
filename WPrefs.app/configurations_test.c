#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "Configurations.c"

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

int GetSpeedForKey(const char *key)
{
	return GetIntegerForKey(key);
}

void SetSpeedForKey(int value, const char *key)
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

void CreateImages(WMScreen *screen, RContext *context, RImage *overlay, const char *name,
		  WMPixmap **normal, WMPixmap **disabled)
{
	char *path = LocateImage(name);

	(void)context;
	(void)overlay;
	/* Test pane sizing/state independently of WPrefs' disabled-artwork composition. */
	*normal = WMCreatePixmapFromFile(screen, path);
	assert(*normal);
	*disabled = WMRetainPixmap(*normal);
	wfree(path);
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

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	int previousTitle = 0, previousSmooth = 0;

	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMWindow *window;
		_Panel *panel;
		WMSize size;

		domain = WMCreatePLDictionary(NULL, NULL);
		SetSpeedForKey(1, "IconSlideSpeed");
		SetSpeedForKey(3, "ShadeSpeed");
		SetStringForKey("old", "NewStyle");
		SetBoolForKey(True, "DisableAnimations");
		SetIntegerForKey(4, "ColormapSize");
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		window = WMCreateWindow(screen, "configurationsTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitConfigurations(window);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));
		assert(WMWidgetWidth(panel->oldsB) > previousTitle);
		assert(WMWidgetWidth(panel->smoB) > previousSmooth);
		previousTitle = WMWidgetWidth(panel->oldsB);
		previousSmooth = WMWidgetWidth(panel->smoB);
		assert(WMGetButtonSelected(panel->oldsB));
		assert(!WMGetButtonSelected(panel->animB));
		assert(WPrefsGetSpeedControlValue(&panel->iconSpeed) == 1);
		assert(WPrefsGetSpeedControlValue(&panel->shadeSpeed) == 3);
		for (int j = 0; j < 5; j++) {
			WMPerformButtonClick(panel->iconSpeed.buttons[j]);
			WMPerformButtonClick(panel->shadeSpeed.buttons[4 - j]);
			storeData(panel);
			assert(GetSpeedForKey("IconSlideSpeed") == j);
			assert(GetSpeedForKey("ShadeSpeed") == 4 - j);
		}
		{
			WMButton *buttons[] = { panel->oldsB, panel->newsB, panel->nextB };
			const char *values[] = { "old", "new", "next" };

			for (int j = 0; j < 3; j++) {
				WMPerformButtonClick(buttons[j]);
				storeData(panel);
				assert(strcmp(GetStringForKey("NewStyle"), values[j]) == 0);
				for (int k = 0; k < 3; k++)
					assert(WMGetButtonSelected(buttons[k]) == (j == k));
			}
		}
		for (int j = 2; j <= 6; j++) {
			char *text;

			WMSetSliderValue(panel->dithS, j);
			updateLabel(panel->dithS, panel);
			text = WMGetLabelText(panel->dithL);
			assert(atoi(text) == j * j * j);
			storeData(panel);
			assert(GetIntegerForKey("ColormapSize") == j);
		}
		WMPerformButtonClick(panel->animB);
		WMPerformButtonClick(panel->supB);
		WMPerformButtonClick(panel->smoB);
		WMPerformButtonClick(panel->dithB);
		storeData(panel);
		assert(!GetBoolForKey("DisableAnimations") && GetBoolForKey("Superfluous"));
		assert(GetBoolForKey("SmoothWorkspaceBack") && GetBoolForKey("DisableDithering"));
		WMResizeWidget(window, size.width + 204, size.height + 204);
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
		puts("SKIP: configurations_test needs an X server");
		return 77;
	}
	WMInitializeApplication("configurations_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware other configurations tests passed");
	return 0;
}
