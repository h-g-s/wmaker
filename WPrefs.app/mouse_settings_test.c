#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "MouseSettings.c"

static WMPropList *domain;
static WMWindow *testWindow;

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

	snprintf(text, sizeof(text), "%d", value);
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

WMWindow *GetWindow(void)
{
	return testWindow;
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
		if (child->self && WMWidgetClass(child->self) == WC_PopUpButton) {
			WMSize size = WMGetPopUpButtonPreferredSize(child->self);

			assert(size.width <= child->size.width && size.height <= child->size.height);
		}
		checkBounds(child);
	}
}

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	int previousImageHeight = 0;
	int originalN, originalD, originalThreshold;
	int originalDelay = WINGsConfiguration.doubleClickDelay;

	XGetPointerControl(screen->display, &originalN, &originalD, &originalThreshold);
	WMSetUDBoolForKey(WMGetStandardUserDefaults(), True, "NoXSetStuff");
	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		_Panel *panel;
		WMSize size;
		int n, d, threshold, beforeDelay;
		char *text;

		domain = WMCreatePLDictionary(NULL, NULL);
		SetIntegerForKey(320, "DoubleClickTime");
		SetStringForKey("Mod1", "ModifierKey");
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		testWindow = WMCreateWindow(screen, "mouseSettingsTest");
		WMRealizeWidget(testWindow);
		panel = (_Panel *)InitMouseSettings(testWindow);
		beforeDelay = WINGsConfiguration.doubleClickDelay;
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		assert(WINGsConfiguration.doubleClickDelay == beforeDelay);
		showPanel(panel);
		assert(WINGsConfiguration.doubleClickDelay == 320);
		XGetPointerControl(screen->display, &n, &d, &threshold);
		assert(n == originalN && d == originalD && threshold == originalThreshold);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(testWindow, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));
		assert(WMGetLabelImage(panel->speedL)->height > previousImageHeight);
		previousImageHeight = WMGetLabelImage(panel->speedL)->height;
		assert(GetDoubleTestPreferredSize(panel->tester).height <= WMWidgetHeight(panel->tester));
		assert(GetDoubleTestPreferredSize(panel->tester).width <= WMWidgetWidth(panel->tester));
		for (int preset = 0; preset < 5; preset++) {
			WMPerformButtonClick(panel->ddelaB[preset]);
			assert(WINGsConfiguration.doubleClickDelay == DELAY(preset));
			storeData(panel);
			assert(GetIntegerForKey("DoubleClickTime") == DELAY(preset));
		}
		SetIntegerForKey(513, "DoubleClickTime");
		showData(panel);
		for (int preset = 0; preset < 5; preset++)
			assert(!WMGetButtonSelected(panel->ddelaB[preset]));
		text = WMGetTextFieldText(panel->ddelaT);
		assert(strcmp(text, "513") == 0);
		wfree(text);
		for (int disabled = 0; disabled < 2; disabled++) {
			WMSetButtonSelected(panel->disaB, disabled);
			toggle_disabling_of_mouse_actions(panel->disaB, panel);
			for (int button = 0; button < wlengthof(button_list); button++)
				assert(WMGetPopUpButtonEnabled(panel->mouse_action[button].popup) == !disabled);
			storeData(panel);
			assert(GetBoolForKey("DisableWSMouseActions") == disabled);
		}
		for (int button = 0; button < wlengthof(button_list); button++) {
			int count = WMGetPopUpButtonNumberOfItems(panel->mouse_action[button].popup);

			for (int action = 0; action < count; action++) {
				WMSetPopUpButtonSelectedItem(panel->mouse_action[button].popup, action);
				storeData(panel);
				assert(strcmp(GetStringForKey(button_list[button].db_key),
					      button_list[button].type == T_BUTTON ? button_actions[action].db_value
					      : wheel_actions[action].db_value) == 0);
			}
		}
		for (int modifier = 0; modifier < WMGetPopUpButtonNumberOfItems(panel->grabP); modifier++) {
			WMSetPopUpButtonSelectedItem(panel->grabP, modifier);
			storeData(panel);
			showData(panel);
			assert(strncmp(WMGetPopUpButtonItem(panel->grabP, WMGetPopUpButtonSelectedItem(panel->grabP)),
				       GetStringForKey("ModifierKey"), strlen(GetStringForKey("ModifierKey"))) == 0);
		}
		WMSetTextFieldText(panel->acceT, "2.50");
		WMSetTextFieldText(panel->threT, "7");
		returnPressed(panel, NULL);
		XGetPointerControl(screen->display, &n, &d, &threshold);
		assert(n * 10 == d * 25 && threshold == 7);
		WMSetSliderValue(panel->speedS, 4);
		speedChange(panel->speedS, panel);
		XGetPointerControl(screen->display, &n, &d, &threshold);
		assert(n * 10 == d * 12 && threshold == 7);
		XChangePointerControl(screen->display, True, True, originalN, originalD, originalThreshold);
		{
			int width = WMWidgetWidth(panel->mouse_action[0].popup);

			WMResizeWidget(testWindow, size.width + 204, size.height + 204);
			assert(WMWidgetWidth(panel->mouse_action[0].popup) > width);
			checkBounds(WMWidgetView(panel->box));
			WMResizeWidget(testWindow, size.width + 4, size.height + 4);
			checkBounds(WMWidgetView(panel->box));
		}
		WMRemoveNotificationObserver(panel);
		WMDestroyWidget(testWindow);
		wfree(panel);
		for (int modifier = 0; modifier < 8; modifier++) {
			wfree(modifierNames[modifier]);
			modifierNames[modifier] = NULL;
		}
		WMReleasePropList(domain);
	}
	WINGsConfiguration.doubleClickDelay = originalDelay;
}

int main(int argc, char **argv)
{
	Display *display;
	WMScreen *screen;

	if (!getenv("DISPLAY")) {
		puts("SKIP: mouse_settings_test needs an X server");
		return 77;
	}
	WMInitializeApplication("mouse_settings_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware mouse preferences tests passed");
	return 0;
}
