/* Autostart.c - commands launched when Window Maker starts
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "WPrefs.h"
#include "PanelLayout.h"

#include <ctype.h>

#define ICON_FILE "autostart"
#define AUTOSTART_KEY "AutostartCommands"
#define MAX_DELAY 86400

enum {
	PRIORITY_LOW,
	PRIORITY_NORMAL,
	PRIORITY_HIGH
};

static const char *const priorityNames[] = { "Low", "Normal", "High" };

typedef struct {
	char *command;
	Bool enabled;
	int priority;
	int delay;
	Bool noRelaunch;
	WMPropList *invalid;
} AutostartItem;

typedef struct _Panel {
	WMBox *box;
	char *sectionName;
	char *description;
	CallbackRec callbacks;
	WMWidget *parent;

	WMList *itemLs;
	WMButton *addB;
	WMButton *removeB;
	WMButton *upB;
	WMButton *downB;

	WMTextField *commandT;
	WMButton *browseB;
	WMButton *enabledB;
	WMPopUpButton *priorityP;
	WMTextField *delayT;
	WMButton *noRelaunchB;

	WMFont *font;
	WMColor *black;
	WMColor *darkGray;
	WMColor *white;
	WMColor *gray;

	Bool updating;
	Bool dirty;
	Bool invalidArray;
} _Panel;

static void freeItem(AutostartItem *item)
{
	if (!item)
		return;
	wfree(item->command);
	if (item->invalid)
		WMReleasePropList(item->invalid);
	wfree(item);
}

static AutostartItem *selectedItem(_Panel *panel)
{
	WMListItem *row = WMGetListSelectedItem(panel->itemLs);

	return row ? row->clientData : NULL;
}

static const char *stringValue(WMPropList *dict, const char *name)
{
	WMPropList *key = WMCreatePLString(name);
	WMPropList *value = WMGetFromPLDictionary(dict, key);

	WMReleasePropList(key);
	return value && WMIsPLString(value) ? WMGetFromPLString(value) : NULL;
}

static Bool boolValue(WMPropList *dict, const char *name, Bool fallback)
{
	const char *text = stringValue(dict, name);

	if (!text)
		return fallback;
	if (!strcasecmp(text, "yes") || !strcasecmp(text, "y") || !strcasecmp(text, "true") || !strcmp(text, "1"))
		return True;
	if (!strcasecmp(text, "no") || !strcasecmp(text, "n") || !strcasecmp(text, "false") || !strcmp(text, "0"))
		return False;
	return fallback;
}

static int parseDelay(const char *text)
{
	long delay;
	char *end;

	while (text && isspace((unsigned char)*text))
		text++;
	if (!text || !*text)
		return 0;
	delay = strtol(text, &end, 10);
	while (isspace((unsigned char)*end))
		end++;
	if (*end || delay < 0)
		return -1;
	return WMIN(delay, MAX_DELAY);
}

static char *itemSummary(AutostartItem *item)
{
	char buffer[160] = "";
	char delay[64];

	if (item->invalid)
		return wstrdup(_("invalid entry; kept unchanged"));
	if (!item->enabled)
		wstrlcat(buffer, _("disabled"), sizeof(buffer));
	if (item->priority != PRIORITY_NORMAL) {
		if (*buffer)
			wstrlcat(buffer, ", ", sizeof(buffer));
		wstrlcat(buffer, item->priority == PRIORITY_LOW ? _("low priority") : _("high priority"),
			 sizeof(buffer));
	}
	if (item->delay > 0) {
		if (*buffer)
			wstrlcat(buffer, ", ", sizeof(buffer));
		snprintf(delay, sizeof(delay), _("after %d s"), item->delay);
		wstrlcat(buffer, delay, sizeof(buffer));
	}
	if (item->noRelaunch) {
		if (*buffer)
			wstrlcat(buffer, ", ", sizeof(buffer));
		wstrlcat(buffer, _("not relaunched"), sizeof(buffer));
	}
	return wstrdup(buffer);
}

static void paintItem(WMList *list, int index, Drawable d, char *text, int state, WMRect *rect)
{
	_Panel *panel = WMGetHangedData(list);
	WMScreen *scr = WMWidgetScreen(list);
	Display *dpy = WMScreenDisplay(scr);
	AutostartItem *item = WMGetListItem(list, index)->clientData;
	WMColor *color = item->enabled && !item->invalid ? panel->black : panel->darkGray;
	int y = rect->pos.y + (rect->size.height - WMFontHeight(panel->font)) / 2;
	int x = rect->pos.x + 4, numberWidth, summaryWidth, available;
	char number[16];
	char *summary = itemSummary(item);
	const char *command = item->invalid ? _("Invalid autostart entry") :
		(*item->command ? item->command : _("New command"));
	int length = strlen(command);

	(void)text;
	XFillRectangle(dpy, d, WMColorGC((state & WLDSSelected) ? panel->white : panel->gray),
		       rect->pos.x, rect->pos.y, rect->size.width, rect->size.height);
	snprintf(number, sizeof(number), "%d.", index + 1);
	numberWidth = WMWidthOfString(panel->font, "000.", 4);
	WMDrawString(scr, d, color, panel->font, x + numberWidth - WMWidthOfString(panel->font, number, strlen(number)),
		     y, number, strlen(number));
	x += numberWidth + 6;
	summaryWidth = *summary ? WMWidthOfString(panel->font, summary, strlen(summary)) : 0;
	available = rect->pos.x + rect->size.width - x - (summaryWidth ? summaryWidth + 16 : 4);
	while (length > 0 && WMWidthOfString(panel->font, command, length) > available)
		length--;
	WMDrawString(scr, d, color, panel->font, x, y, command, length);
	if (summaryWidth)
		WMDrawString(scr, d, panel->darkGray, panel->font,
			     rect->pos.x + rect->size.width - summaryWidth - 4, y, summary, strlen(summary));
	wfree(summary);
}

static void updateControls(_Panel *panel)
{
	AutostartItem *item = selectedItem(panel);
	int row = WMGetListSelectedItemRow(panel->itemLs);
	int rows = WMGetListNumberOfRows(panel->itemLs);
	Bool editable = item && !item->invalid;
	char delay[16];

	panel->updating = True;
	WMSetTextFieldText(panel->commandT, editable ? item->command : "");
	WMSetButtonSelected(panel->enabledB, editable ? item->enabled : False);
	WMSetPopUpButtonSelectedItem(panel->priorityP, editable ? item->priority : PRIORITY_NORMAL);
	snprintf(delay, sizeof(delay), "%d", editable ? item->delay : 0);
	WMSetTextFieldText(panel->delayT, editable ? delay : "");
	WMSetButtonSelected(panel->noRelaunchB, editable ? item->noRelaunch : False);
	panel->updating = False;

	WMSetTextFieldEditable(panel->commandT, editable);
	WMSetTextFieldEditable(panel->delayT, editable);
	WMSetButtonEnabled(panel->browseB, editable);
	WMSetButtonEnabled(panel->enabledB, editable);
	WMSetPopUpButtonEnabled(panel->priorityP, editable);
	WMSetButtonEnabled(panel->noRelaunchB, editable);
	WMSetButtonEnabled(panel->addB, !panel->invalidArray);
	WMSetButtonEnabled(panel->removeB, item != NULL);
	WMSetButtonEnabled(panel->upB, item != NULL && row > 0);
	WMSetButtonEnabled(panel->downB, item != NULL && row + 1 < rows);
}

static void itemChanged(_Panel *panel)
{
	panel->dirty = True;
	WMRedisplayWidget(panel->itemLs);
}

static void editorChanged(void *data, WMNotification *notification)
{
	_Panel *panel = data;
	AutostartItem *item = selectedItem(panel);
	char *text;
	int delay;

	(void)notification;
	if (panel->updating || !item || item->invalid)
		return;
	text = WMGetTextFieldText(panel->commandT);
	wfree(item->command);
	item->command = text;
	text = WMGetTextFieldText(panel->delayT);
	delay = parseDelay(text);
	wfree(text);
	if (delay >= 0)
		item->delay = delay;
	itemChanged(panel);
}

static void optionChanged(WMWidget *w, void *data)
{
	_Panel *panel = data;
	AutostartItem *item = selectedItem(panel);

	(void)w;
	if (panel->updating || !item || item->invalid)
		return;
	item->enabled = WMGetButtonSelected(panel->enabledB);
	item->priority = WMGetPopUpButtonSelectedItem(panel->priorityP);
	item->noRelaunch = WMGetButtonSelected(panel->noRelaunchB);
	itemChanged(panel);
}

static void insertItem(_Panel *panel, int row, AutostartItem *item)
{
	WMListItem *listItem = WMInsertListItem(panel->itemLs, row, "");

	listItem->clientData = item;
}

static void listClick(WMWidget *w, void *data)
{
	(void)w;
	updateControls(data);
}

static void moveSelected(_Panel *panel, int offset)
{
	int row = WMGetListSelectedItemRow(panel->itemLs);
	int target = row + offset;
	AutostartItem *item;

	if (row < 0 || target < 0 || target >= WMGetListNumberOfRows(panel->itemLs))
		return;
	item = WMGetListItem(panel->itemLs, row)->clientData;
	WMRemoveListItem(panel->itemLs, row);
	insertItem(panel, target, item);
	WMSelectListItem(panel->itemLs, target);
	WMSetListPosition(panel->itemLs, WMIN(target, WMAX(0, target - 2)));
	itemChanged(panel);
	updateControls(panel);
}

static void buttonClick(WMWidget *w, void *data)
{
	_Panel *panel = data;
	int row = WMGetListSelectedItemRow(panel->itemLs);

	if (w == panel->addB) {
		AutostartItem *item = wmalloc(sizeof(*item));

		item->command = wstrdup("");
		item->enabled = True;
		item->priority = PRIORITY_NORMAL;
		row = row >= 0 ? row + 1 : WMGetListNumberOfRows(panel->itemLs);
		insertItem(panel, row, item);
		WMSelectListItem(panel->itemLs, row);
		WMSetListBottomPosition(panel->itemLs, row);
		itemChanged(panel);
		updateControls(panel);
		WMSetFocusToWidget(panel->commandT);
		return;
	} else if (w == panel->removeB && row >= 0) {
		freeItem(WMGetListItem(panel->itemLs, row)->clientData);
		WMRemoveListItem(panel->itemLs, row);
		if (WMGetListNumberOfRows(panel->itemLs) > 0)
			WMSelectListItem(panel->itemLs, WMIN(row, WMGetListNumberOfRows(panel->itemLs) - 1));
		itemChanged(panel);
	} else if (w == panel->upB) {
		moveSelected(panel, -1);
		return;
	} else if (w == panel->downB) {
		moveSelected(panel, 1);
		return;
	}
	updateControls(panel);
}

static void browseClick(WMWidget *w, void *data)
{
	_Panel *panel = data;
	WMFilePanel *filePanel = WMGetOpenPanel(WMWidgetScreen(w));
	char *text = WMGetTextFieldText(panel->commandT);
	char *directory = wtrimspace(text);
	char *slash;

	wfree(text);
	slash = directory[0] == '/' ? strrchr(directory, '/') : NULL;
	if (slash && !strpbrk(directory, " \t"))
		slash[slash == directory ? 1 : 0] = 0;
	else {
		wfree(directory);
		directory = wstrdup("/usr/bin");
	}
	WMSetFilePanelCanChooseDirectories(filePanel, False);
	if (WMRunModalFilePanelForDirectory(filePanel, panel->parent, directory, _("Select Program"), NULL)) {
		char *path = WMGetFilePanelFileName(filePanel);

		WMSetTextFieldText(panel->commandT, path);
		editorChanged(panel, NULL);
		wfree(path);
	}
	wfree(directory);
}

static void clearItems(_Panel *panel)
{
	while (WMGetListNumberOfRows(panel->itemLs) > 0) {
		freeItem(WMGetListItem(panel->itemLs, 0)->clientData);
		WMRemoveListItem(panel->itemLs, 0);
	}
}

static void showData(_Panel *panel)
{
	WMPropList *array = GetObjectForKey(AUTOSTART_KEY);
	int i;

	clearItems(panel);
	panel->invalidArray = array && !WMIsPLArray(array);
	if (panel->invalidArray)
		wwarning(_("Wrong option format for %s; expected an array."), AUTOSTART_KEY);
	for (i = 0; array && !panel->invalidArray && i < WMGetPropListItemCount(array); i++) {
		WMPropList *entry = WMGetFromPLArray(array, i);
		AutostartItem *item = wmalloc(sizeof(*item));
		const char *command = WMIsPLDictionary(entry) ? stringValue(entry, "Command") : NULL;
		const char *priority;
		int delay;

		if (!command) {
			item->command = wstrdup("");
			item->invalid = WMRetainPropList(entry);
			insertItem(panel, -1, item);
			continue;
		}
		item->command = wstrdup(command);
		item->enabled = boolValue(entry, "Enabled", True);
		item->noRelaunch = boolValue(entry, "NoRelaunch", False);
		priority = stringValue(entry, "Priority");
		item->priority = PRIORITY_NORMAL;
		if (priority && !strcasecmp(priority, "Low"))
			item->priority = PRIORITY_LOW;
		else if (priority && !strcasecmp(priority, "High"))
			item->priority = PRIORITY_HIGH;
		delay = parseDelay(stringValue(entry, "Delay"));
		item->delay = WMAX(delay, 0);
		insertItem(panel, -1, item);
	}
	if (WMGetListNumberOfRows(panel->itemLs) > 0)
		WMSelectListItem(panel->itemLs, 0);
	panel->dirty = False;
	updateControls(panel);
}

static void setString(WMPropList *dict, const char *name, const char *value)
{
	WMPropList *key = WMCreatePLString(name);
	WMPropList *string = WMCreatePLString(value);

	WMPutInPLDictionary(dict, key, string);
	WMReleasePropList(key);
	WMReleasePropList(string);
}

static void storeData(_Panel *panel)
{
	WMPropList *array;
	int i;

	if (!panel->dirty || panel->invalidArray)
		return;
	array = WMCreatePLArray(NULL);
	for (i = 0; i < WMGetListNumberOfRows(panel->itemLs); i++) {
		AutostartItem *item = WMGetListItem(panel->itemLs, i)->clientData;
		WMPropList *dict;
		char *command, delay[16];

		if (item->invalid) {
			WMAddToPLArray(array, item->invalid);
			continue;
		}
		command = wtrimspace(item->command);
		if (!*command) {
			wfree(command);
			continue;
		}
		dict = WMCreatePLDictionary(NULL, NULL);
		setString(dict, "Command", command);
		setString(dict, "Enabled", item->enabled ? "YES" : "NO");
		setString(dict, "Priority", priorityNames[item->priority]);
		snprintf(delay, sizeof(delay), "%d", item->delay);
		setString(dict, "Delay", delay);
		setString(dict, "NoRelaunch", item->noRelaunch ? "YES" : "NO");
		WMAddToPLArray(array, dict);
		WMReleasePropList(dict);
		wfree(command);
	}
	SetObjectForKey(array, AUTOSTART_KEY);
	WMReleasePropList(array);
	panel->dirty = False;
}

static WMButton *createButton(WMWidget *parent, const char *title, _Panel *panel)
{
	WMButton *button = WMCreateCommandButton(parent);

	WMSetButtonText(button, title);
	WMSetButtonAction(button, buttonClick, panel);
	return button;
}

static WMBox *createRow(WMBox *parent)
{
	WMBox *row = WMCreateBox(parent);

	WMSetBoxHorizontal(row, True);
	return row;
}

static void packRow(WMBox *parent, WMBox *row, int space)
{
	WMPackBoxSubview(parent, row, WMGetBoxMinimumSize(row), False, space);
	WMMapSubwidgets(row);
}

static void createPanel(Panel *p)
{
	_Panel *panel = (_Panel *)p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WPrefsGroup order, editor;
	WMBox *listRow, *buttons, *row, *spacer;
	WMLabel *label;
	WMSize buttonSize = wmksize(0, 0), size;
	WMButton *listButtons[4];
	int rowHeight, i;

	panel->font = WMDefaultSystemFont(scr);
	panel->black = WMBlackColor(scr);
	panel->darkGray = WMDarkGrayColor(scr);
	panel->white = WMWhiteColor(scr);
	panel->gray = WMGrayColor(scr);
	rowHeight = WMFontHeight(panel->font) + 6;

	panel->box = WPrefsCreatePanelBox(panel->parent);

	order = WPrefsCreateGroup(panel->box, _("Startup Order"));
	listRow = createRow(order.content);
	panel->itemLs = WMCreateList(listRow);
	WMHangData(panel->itemLs, panel);
	WMSetListUserDrawItemHeight(panel->itemLs, rowHeight);
	WMSetListUserDrawProc(panel->itemLs, paintItem);
	WMSetListAction(panel->itemLs, listClick, panel);
	WMSetBalloonTextForView(_("Commands start from top to bottom when Window Maker starts. "
				  "A delay postpones a command without holding back the ones below it."),
				WMWidgetView(panel->itemLs));
	WMPackBoxSubview(listRow, panel->itemLs,
			 wmksize(WMWidthOfString(panel->font, "M", 1) * 46, rowHeight * 8 + 4), True, 10);
	buttons = WMCreateBox(listRow);
	panel->addB = listButtons[0] = createButton(buttons, _("Add"), panel);
	panel->removeB = listButtons[1] = createButton(buttons, _("Remove"), panel);
	panel->upB = listButtons[2] = createButton(buttons, _("Move Up"), panel);
	panel->downB = listButtons[3] = createButton(buttons, _("Move Down"), panel);
	for (i = 0; i < 4; i++) {
		size = WMGetButtonPreferredSize(listButtons[i]);
		buttonSize.width = WMAX(buttonSize.width, size.width);
		buttonSize.height = WMAX(buttonSize.height, size.height);
	}
	for (i = 0; i < 4; i++)
		WMPackBoxSubview(buttons, listButtons[i], buttonSize, False, i == 1 ? 14 : 6);
	spacer = WMCreateBox(buttons);
	WMPackBoxSubview(buttons, spacer, wmksize(1, 1), True, 0);
	WMPackBoxSubview(listRow, buttons, WMGetBoxMinimumSize(buttons), False, 0);
	WMMapSubwidgets(buttons);
	WMPackBoxSubview(order.content, listRow, WMGetBoxMinimumSize(listRow), True, 0);
	WMMapSubwidgets(listRow);
	WPrefsPackGroup(panel->box, &order, True, 10);

	editor = WPrefsCreateGroup(panel->box, _("Selected Command"));
	row = createRow(editor.content);
	label = WMCreateLabel(row);
	WMSetLabelText(label, _("Command:"));
	WMSetLabelTextAlignment(label, WARight);
	size = WMGetLabelPreferredSize(label);
	panel->commandT = WMCreateTextField(row);
	WMSetBalloonTextForView(_("The command is run by /bin/sh in the background; do not add a trailing \"&\"."),
				WMWidgetView(panel->commandT));
	WMAddNotificationObserver(editorChanged, panel, WMTextDidChangeNotification, panel->commandT);
	panel->browseB = WMCreateCommandButton(row);
	WMSetButtonText(panel->browseB, _("Browse..."));
	WMSetButtonAction(panel->browseB, browseClick, panel);
	size.height = WMGetTextFieldPreferredSize(panel->commandT, 30).height;
	WMPackBoxSubview(row, label, size, False, 6);
	WMPackBoxSubview(row, panel->commandT, WMGetTextFieldPreferredSize(panel->commandT, 30), True, 8);
	WPrefsPackButton(row, panel->browseB, 0);
	packRow(editor.content, row, 10);

	row = createRow(editor.content);
	panel->enabledB = WMCreateSwitchButton(row);
	WMSetButtonText(panel->enabledB, _("Enabled"));
	WMSetButtonAction(panel->enabledB, optionChanged, panel);
	WPrefsPackButton(row, panel->enabledB, 20);
	label = WMCreateLabel(row);
	WMSetLabelText(label, _("Priority:"));
	size = WMGetLabelPreferredSize(label);
	panel->priorityP = WMCreatePopUpButton(row);
	WMAddPopUpButtonItem(panel->priorityP, _("Low"));
	WMAddPopUpButtonItem(panel->priorityP, _("Normal"));
	WMAddPopUpButtonItem(panel->priorityP, _("High"));
	WMSetPopUpButtonAction(panel->priorityP, optionChanged, panel);
	WMSetBalloonTextForView(_("Low runs the command with nice 10. High lowers the nice value by 5,\n"
				  "which usually requires extra privileges; otherwise it runs normally."),
				WMWidgetView(panel->priorityP));
	size.height = WMGetPopUpButtonPreferredSize(panel->priorityP).height;
	WMPackBoxSubview(row, label, size, False, 4);
	WMPackBoxSubview(row, panel->priorityP, WMGetPopUpButtonPreferredSize(panel->priorityP), False, 20);
	label = WMCreateLabel(row);
	WMSetLabelText(label, _("Start delay:"));
	WMPackBoxSubview(row, label, wmksize(WMGetLabelPreferredSize(label).width, size.height), False, 4);
	panel->delayT = WMCreateTextField(row);
	WMSetTextFieldAlignment(panel->delayT, WARight);
	WMAddNotificationObserver(editorChanged, panel, WMTextDidChangeNotification, panel->delayT);
	WMSetBalloonTextForView(_("Seconds to wait after Window Maker starts before running the command."),
				WMWidgetView(panel->delayT));
	WMPackBoxSubview(row, panel->delayT,
			 wmksize(WMGetTextFieldPreferredSize(panel->delayT, 5).width, size.height), False, 4);
	label = WMCreateLabel(row);
	WMSetLabelText(label, _("seconds"));
	WMPackBoxSubview(row, label, wmksize(WMGetLabelPreferredSize(label).width, size.height), False, 0);
	packRow(editor.content, row, 8);

	row = createRow(editor.content);
	panel->noRelaunchB = WMCreateSwitchButton(row);
	WMSetButtonText(panel->noRelaunchB, _("Don't relaunch on restart if it is already running"));
	WMSetButtonAction(panel->noRelaunchB, optionChanged, panel);
	WMSetBalloonTextForView(_("When Window Maker restarts, skip this command if a process with the same\n"
				  "program name is still running. Useful for system tray applications."),
				WMWidgetView(panel->noRelaunchB));
	WPrefsPackButton(row, panel->noRelaunchB, 0);
	packRow(editor.content, row, 8);

	label = WMCreateLabel(editor.content);
	WMSetLabelWraps(label, True);
	WMSetLabelText(label, _("Commands run in the background and never delay startup. "
				"The autostart script is still run before this list."));
	size = WMGetLabelPreferredSizeForWidth(label, WMGetBoxMinimumSize(editor.content).width);
	WMPackBoxSubview(editor.content, label, size, False, 0);
	WPrefsPackGroup(panel->box, &editor, False, 0);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);
	showData(panel);
}

Panel *InitAutostart(WMWidget *parent)
{
	_Panel *panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Autostart Preferences");
	panel->description = _("Commands started with Window Maker, their order,\n"
			       "priority, delay and restart behavior.");
	panel->parent = parent;
	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);
	return panel;
}
