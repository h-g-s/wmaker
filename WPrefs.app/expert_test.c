#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "Expert.c"

static WMPropList *domain;

char *LocateImage(const char *name)
{
	(void)name;
	return NULL;
}

char *GetStringForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMGetFromPLDictionary(domain, name);

	WMReleasePropList(name);
	return value && WMIsPLString(value) ? WMGetFromPLString(value) : NULL;
}

void SetStringForKey(const char *text, const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMCreatePLString(text);

	WMPutInPLDictionary(domain, name, value);
	WMReleasePropList(name);
	WMReleasePropList(value);
}

void RemoveObjectForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);

	WMRemoveFromPLDictionary(domain, name);
	WMReleasePropList(name);
}

int GetIntegerForKey(const char *key)
{
	char *text = GetStringForKey(key);

	return text ? atoi(text) : 0;
}

void SetIntegerForKey(int value, const char *key)
{
	char text[32];

	snprintf(text, sizeof(text), "%d", value);
	SetStringForKey(text, key);
}

Bool GetBoolForKey(const char *key)
{
	char *text = GetStringForKey(key);

	return text && strcmp(text, "YES") == 0;
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

static void checkBounds(WMView *view)
{
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		if (child->self && WMWidgetClass(child->self) == WC_Button) {
			WMSize size = WMGetButtonPreferredSizeForWidth(child->self, child->size.width);

			assert(size.height <= child->size.height);
		}
		if (child->self && WMWidgetClass(child->self) == WC_Label) {
			WMSize size = WMGetLabelPreferredSizeForWidth(child->self, child->size.width);

			assert(size.height <= child->size.height);
		}
		checkBounds(child);
	}
}

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	WMUserDefaults *udb = WMGetStandardUserDefaults();

	for (int n = 0; n < 3; n++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[n]);
		WMWindow *window;
		_Panel *panel;
		WMSize size;
		int arrayRow = -1;
		XEvent event = { 0 };
		WMRect before, after;

		domain = WMCreatePLDictionary(NULL, NULL);
		WMSetUDBoolForKey(udb, True, "NoXSetStuff");
		SetIntegerForKey(17, "SnapEdgeDetect");
		SetStringForKey("None", "SwitchPanelImages");
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		window = WMCreateWindow(screen, "expertTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitExpert(window);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		assert(WMGetUDBoolForKey(udb, "NoXSetStuff"));
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->frame));
		for (int i = 0; i < wlengthof(expert_options); i++) {
			const struct expert_option *option = &expert_options[i];

			if (option->class == OPTION_WMAKER_INT) {
				WMPerformButtonClick(panel->up[i]);
				storeDefaults(panel);
				assert(GetIntegerForKey(option->op_name) ==
				       (strcmp(option->op_name, "SnapEdgeDetect") == 0 ? 18 : option->def_state + 1));
				WMPerformButtonClick(panel->down[i]);
				storeDefaults(panel);
				assert(GetIntegerForKey(option->op_name) ==
				       (strcmp(option->op_name, "SnapEdgeDetect") == 0 ? 17 : option->def_state));
			} else if (option->class == OPTION_WMAKER_ARRAY) {
				arrayRow = i;
				assert(!WMGetButtonSelected(panel->swi[i]));
			} else {
				WMSetButtonSelected(panel->swi[i], !WMGetButtonSelected(panel->swi[i]));
				storeDefaults(panel);
				if (option->class == OPTION_USERDEF)
					assert(!WMGetUDBoolForKey(udb, option->op_name));
				else
					assert(GetBoolForKey(option->op_name) == !option->def_state);
			}
		}
		assert(arrayRow >= 0);
		WMSetButtonSelected(panel->swi[arrayRow], True);
		storeDefaults(panel);
		assert(!GetStringForKey("SwitchPanelImages"));
		{
			WMPropList *key = WMCreatePLString("SwitchPanelImages");
			WMPropList *array = WMCreatePropListFromDescription("(custom, images)");

			WMPutInPLDictionary(domain, key, array);
			storeDefaults(panel);
			assert(WMGetFromPLDictionary(domain, key) == array);
			WMReleasePropList(array);
			WMReleasePropList(key);
		}
		event.xbutton.type = ButtonPress;
		event.xbutton.button = WINGsConfiguration.mouseWheelDown;
		before = WMGetScrollViewVisibleRect(panel->sv);
		scrollViewWheelHandler(&event, panel);
		after = WMGetScrollViewVisibleRect(panel->sv);
		assert(after.pos.y > before.pos.y);
		event.xbutton.state = ControlMask;
		for (int i = 0; i < 20; i++)
			scrollViewWheelHandler(&event, panel);
		after = WMGetScrollViewVisibleRect(panel->sv);
		assert(after.pos.y + after.size.height == WMWidgetHeight(panel->frame));
		event.xbutton.button = WINGsConfiguration.mouseWheelUp;
		for (int i = 0; i < 20; i++)
			scrollViewWheelHandler(&event, panel);
		assert(WMGetScrollViewVisibleRect(panel->sv).pos.y == 0);
		{
			int width = WMWidgetWidth(panel->frame);

			WMResizeWidget(window, size.width + 204, size.height + 204);
			assert(WMWidgetWidth(panel->frame) > width);
			checkBounds(WMWidgetView(panel->frame));
			WMResizeWidget(window, size.width + 4, size.height + 4);
			checkBounds(WMWidgetView(panel->frame));
		}
		scrollViewPrepareForClose((Panel *)panel);
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
		puts("SKIP: expert_test needs an X server");
		return 77;
	}
	WMInitializeApplication("expert_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware expert preferences tests passed");
	return 0;
}
