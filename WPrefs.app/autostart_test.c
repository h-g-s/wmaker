#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "Autostart.c"

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
		checkBounds(child);
	}
}

static AutostartItem *row(_Panel *panel, int index)
{
	return WMGetListItem(panel->itemLs, index)->clientData;
}

static const char *stored(int index, const char *key)
{
	return stringValue(WMGetFromPLArray(GetObjectForKey(AUTOSTART_KEY), index), key);
}

static void selectRow(_Panel *panel, int index)
{
	WMSelectListItem(panel->itemLs, index);
	listClick(panel->itemLs, panel);
}

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24 };

	for (int i = 0; i < 2; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMWindow *window;
		_Panel *panel;
		WMSize size;
		char *summary;

		domain = WMCreatePLDictionary(NULL, NULL);
		SetObjectForKey(WMCreatePropListFromDescription(
			"({Command = \"picom --backend glx --vsync\"; NoRelaunch = YES;},"
			" {Command = \"xscreensaver\"; Delay = 5; Priority = Low;},"
			" broken,"
			" {Command = \"xset b off\"; Enabled = NO; Priority = High; Delay = bogus;})"), AUTOSTART_KEY);
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		window = WMCreateWindow(screen, "autostartTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitAutostart(window);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		size = WMGetBoxMinimumSize(panel->box);
		WMResizeWidget(window, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));

		assert(WMGetListNumberOfRows(panel->itemLs) == 4);
		assert(row(panel, 0)->noRelaunch && row(panel, 0)->enabled && row(panel, 0)->delay == 0);
		assert(row(panel, 1)->priority == PRIORITY_LOW && row(panel, 1)->delay == 5);
		assert(row(panel, 2)->invalid);
		assert(!row(panel, 3)->enabled && row(panel, 3)->priority == PRIORITY_HIGH && row(panel, 3)->delay == 0);
		summary = itemSummary(row(panel, 1));
		assert(strstr(summary, "low priority") && strstr(summary, "after 5 s"));
		wfree(summary);
		assert(!WMGetButtonEnabled(panel->upB) && WMGetButtonEnabled(panel->downB));

		/* Unchanged panels leave the defaults alone. */
		storeData(panel);
		assert(WMIsPLString(WMGetFromPLArray(GetObjectForKey(AUTOSTART_KEY), 2)));

		selectRow(panel, 2);
		assert(!WMGetButtonEnabled(panel->enabledB) && WMGetButtonEnabled(panel->removeB));
		selectRow(panel, 1);
		WMSetTextFieldText(panel->delayT, "12");
		editorChanged(panel, NULL);
		WMSetTextFieldText(panel->delayT, "x");
		editorChanged(panel, NULL);
		assert(row(panel, 1)->delay == 12);
		WMSetPopUpButtonSelectedItem(panel->priorityP, PRIORITY_HIGH);
		WMSetButtonSelected(panel->noRelaunchB, True);
		optionChanged(panel->priorityP, panel);
		WMPerformButtonClick(panel->upB);
		assert(WMGetListSelectedItemRow(panel->itemLs) == 0);
		assert(!strcmp(row(panel, 0)->command, "xscreensaver"));
		assert(!WMGetButtonEnabled(panel->upB));

		selectRow(panel, 3);
		assert(!WMGetButtonEnabled(panel->downB));
		WMPerformButtonClick(panel->addB);
		assert(WMGetListNumberOfRows(panel->itemLs) == 5 && WMGetListSelectedItemRow(panel->itemLs) == 4);
		WMSetTextFieldText(panel->commandT, "ulauncher --hide-window");
		editorChanged(panel, NULL);
		WMSetButtonSelected(panel->enabledB, False);
		optionChanged(panel->enabledB, panel);
		WMPerformButtonClick(panel->addB);
		selectRow(panel, 3);
		WMPerformButtonClick(panel->removeB);
		assert(WMGetListNumberOfRows(panel->itemLs) == 5);
		checkBounds(WMWidgetView(panel->box));

		/* Blank commands are dropped; invalid entries are kept in place. */
		storeData(panel);
		assert(WMGetPropListItemCount(GetObjectForKey(AUTOSTART_KEY)) == 4);
		assert(!strcmp(stored(0, "Command"), "xscreensaver"));
		assert(!strcmp(stored(0, "Priority"), "High"));
		assert(!strcmp(stored(0, "Delay"), "12"));
		assert(!strcmp(stored(0, "NoRelaunch"), "YES"));
		assert(!strcmp(stored(1, "Command"), "picom --backend glx --vsync"));
		assert(WMIsPLString(WMGetFromPLArray(GetObjectForKey(AUTOSTART_KEY), 2)));
		assert(!strcmp(stored(3, "Command"), "ulauncher --hide-window"));
		assert(!strcmp(stored(3, "Enabled"), "NO"));
		assert(!strcmp(stored(3, "Priority"), "Normal"));

		showData(panel);
		assert(WMGetListNumberOfRows(panel->itemLs) == 4);
		assert(row(panel, 0)->priority == PRIORITY_HIGH && row(panel, 0)->noRelaunch);
		assert(!panel->dirty);

		SetObjectForKey(WMCreatePLString("invalid"), AUTOSTART_KEY);
		showData(panel);
		assert(WMGetListNumberOfRows(panel->itemLs) == 0 && !WMGetButtonEnabled(panel->addB));
		storeData(panel);
		assert(WMIsPLString(GetObjectForKey(AUTOSTART_KEY)));

		WMRemoveNotificationObserver(panel);
		clearItems(panel);
		WMDestroyWidget(window);
		WMReleaseFont(panel->font);
		WMReleaseColor(panel->black);
		WMReleaseColor(panel->darkGray);
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
		puts("SKIP: autostart_test needs an X server");
		return 77;
	}
	WMInitializeApplication("autostart_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Autostart panel tests passed");
	return 0;
}
