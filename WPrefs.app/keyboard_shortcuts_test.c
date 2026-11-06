#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "KeyboardShortcuts.c"

static WMPropList *domain;
static WMWindow *testWindow;

char *GetStringForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMGetFromPLDictionary(domain, name);

	WMReleasePropList(name);
	return value ? WMGetFromPLString(value) : NULL;
}

void SetStringForKey(const char *text, const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMCreatePLString(text);

	WMPutInPLDictionary(domain, name, value);
	WMReleasePropList(name);
	WMReleasePropList(value);
}

int GetIntegerForKey(const char *key)
{
	(void)key;
	return 10;
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

static int actionRow(_Panel *panel, const char *key)
{
	for (int i = 0; i < panel->actionCount; i++)
		if (strcmp(keyOptions[i].key, key) == 0)
			return i;
	assert(0);
	return -1;
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

static void checkInstructions(_Panel *panel)
{
	const char *texts[] = { shortcutInstructions, captureInstructions };

	for (int i = 0; i < 2; i++) {
		WMSize size;

		WMSetLabelText(panel->instructionsL, _(texts[i]));
		size = WMGetLabelPreferredSizeForWidth(panel->instructionsL, WMWidgetWidth(panel->instructionsL));
		assert(size.height <= WMWidgetHeight(panel->instructionsL));
	}
	WMSetLabelText(panel->instructionsL, _(shortcutInstructions));
	WMSetButtonText(panel->defB, _("Cancel"));
	checkBounds(WMWidgetView(panel->box));
	WMSetButtonText(panel->defB, _("Capture"));
}

static void queueChord(WMScreen *screen, KeySym key, unsigned int modifiers)
{
	XEvent event = { 0 };

	event.xkey.type = KeyPress;
	event.xkey.display = screen->display;
	event.xkey.window = WMWidgetXID(testWindow);
	event.xkey.keycode = XKeysymToKeycode(screen->display, key);
	event.xkey.state = modifiers;
	assert(event.xkey.keycode);
	XPutBackEvent(screen->display, &event);
}

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };

	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMFont *boldFont = WMBoldSystemFontOfSize(screen, sizes[i]);
		_Panel *panel;
		WMSize size;
		int root, run, listHeight, listWidth;
		char *text;

		domain = WMCreatePLDictionary(NULL, NULL);
		SetStringForKey("  Control+F12  ", "RootMenuKey");
		SetStringForKey("none", "RunKey");
		SetStringForKey("Control+X Control+W", "WindowMenuKey");
		WMSetWidgetDefaultFont(screen, font);
		WMSetWidgetDefaultBoldFont(screen, boldFont);
		WMReleaseFont(font);
		WMReleaseFont(boldFont);
		testWindow = WMCreateWindow(screen, "keyboardShortcutsTest");
		WMRealizeWidget(testWindow);
		panel = (_Panel *)InitKeyboardShortcuts(testWindow);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(testWindow, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));
		checkInstructions(panel);
		assert(WMGetListItemHeight(panel->actLs) == WMFontHeight(panel->font) + 4);
		for (int row = 0; row < panel->actionCount; row++)
			assert(WMWidthOfString(panel->font, _(keyOptions[row].title),
					      strlen(_(keyOptions[row].title))) + panel->indicatorWidth + 24
			       <= WMWidgetWidth(panel->actLs));

		root = actionRow(panel, "RootMenuKey");
		run = actionRow(panel, "RunKey");
		assert(strcmp(panel->shortcuts[root], "Control+F12") == 0);
		assert(!panel->shortcuts[run]);
		WMSelectListItem(panel->actLs, root);
		listClick(panel->actLs, panel);
		text = WMGetTextFieldText(panel->shoT);
		assert(strcmp(text, "Control+F12") == 0);
		wfree(text);
		assert(isKeySet(panel, "Control+F12") == root);
		WMPerformButtonClick(panel->cleB);
		assert(!panel->shortcuts[root]);
		WMSetTextFieldText(panel->shoT, "Mod1+F10");
		typedKeys(panel, NULL);
		assert(strcmp(panel->shortcuts[root], "Mod1+F10") == 0);

		WMSelectListItem(panel->actLs, run);
		queueChord(screen, XK_c, 0);
		queueChord(screen, XK_x, ControlMask);
		captureClick(panel->defB, panel);
		assert(!panel->capturing);
		assert(strcmp(panel->shortcuts[run], "Control+X C") == 0);
		assert(strcmp(WMGetButtonText(panel->defB), _("Capture")) == 0);
		checkInstructions(panel);
		storeData(panel);
		assert(strcmp(GetStringForKey("RootMenuKey"), "Mod1+F10") == 0);
		assert(strcmp(GetStringForKey("RunKey"), "Control+X C") == 0);
		assert(strcmp(GetStringForKey("WindowMenuKey"), "Control+X Control+W") == 0);
		WMPerformButtonClick(panel->cleB);
		storeData(panel);
		assert(strcmp(GetStringForKey("RunKey"), "None") == 0);
		SetStringForKey("Mod4+R", "RunKey");
		showData(panel);
		assert(strcmp(panel->shortcuts[run], "Mod4+R") == 0);

		listHeight = WMWidgetHeight(panel->actLs);
		listWidth = WMWidgetWidth(panel->actLs);
		WMResizeWidget(testWindow, size.width + 204, size.height + 204);
		assert(WMWidgetHeight(panel->actLs) > listHeight);
		assert(WMWidgetWidth(panel->actLs) > listWidth);
		checkBounds(WMWidgetView(panel->box));
		WMResizeWidget(testWindow, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));
		WMRemoveNotificationObserver(panel);
		WMDestroyWidget(testWindow);
		for (int row = 0; row < panel->actionCount; row++)
			if (panel->shortcuts[row])
				wfree(panel->shortcuts[row]);
		wfree(panel->shortcuts);
		WMReleaseFont(panel->font);
		WMReleaseColor(panel->black);
		WMReleaseColor(panel->white);
		WMReleaseColor(panel->gray);
		wfree(panel);
		WMReleasePropList(domain);
	}
}

int main(int argc, char **argv)
{
	Display *display;
	WMScreen *screen;

	if (!getenv("DISPLAY")) {
		puts("SKIP: keyboard_shortcuts_test needs an X server");
		return 77;
	}
	WMInitializeApplication("keyboard_shortcuts_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware keyboard shortcut tests passed");
	return 0;
}
