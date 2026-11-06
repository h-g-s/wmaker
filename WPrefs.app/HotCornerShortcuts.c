/* HotCornerShortcuts.c - screen corners actions
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 2023 Window Maker Team
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
#include "FontControls.h"
#include "PanelLayout.h"

typedef struct _Panel {
	WMBox *box;
	char *sectionName;
	char *description;
	CallbackRec callbacks;
	WMWidget *parent;

	WMFrame *hcF;
	WMButton *hcB;
	WMFrame *hceF;
	WMSlider *hceS;
	WMLabel *hceL;
	WMFrame *hcdescF;
	WMLabel *hcdescL;

	WMFrame *hcdelayF;
	WMButton *hcdelayB[5];
	WMTextField *hcdelayT;
	WMLabel *hcdelayL;
	WMLabel *icornerL[4];

	WMFrame *hcactionsF;
	WMTextField *hcactionsT[4];
	Bool hotcornerDelaySelected;
} _Panel;

#define ICON_FILE "hotcorners"

static const int delayValues[] = { 0, 250, 500, 750, 1000 };

static void edgeCallback(WMWidget *w, void *data)
{
	_Panel *panel = data;
	char buffer[64];
	int value = WMGetSliderValue(panel->hceS);

	(void)w;
	if (value == 0)
		snprintf(buffer, sizeof(buffer), "%s", _("OFF"));
	else if (value == 1)
		snprintf(buffer, sizeof(buffer), "%s", _("1 pixel"));
	else
		snprintf(buffer, sizeof(buffer), _("%i pixels"), value);
	WMSetLabelText(panel->hceL, buffer);
}

static void showData(_Panel *panel)
{
	int value = GetObjectForKey("HotCornerDelay") ? GetIntegerForKey("HotCornerDelay") : 250;
	char buffer[32];
	WMPropList *array;

	snprintf(buffer, sizeof(buffer), "%i", value);
	WMSetTextFieldText(panel->hcdelayT, buffer);
	panel->hotcornerDelaySelected = False;
	for (int i = 0; i < wlengthof(panel->hcdelayB); i++)
		WMSetButtonSelected(panel->hcdelayB[i], False);
	for (int i = 0; i < wlengthof(delayValues); i++) {
		if (value == delayValues[i]) {
			WMSetButtonSelected(panel->hcdelayB[i], True);
			panel->hotcornerDelaySelected = True;
		}
	}

	value = GetIntegerForKey("HotCornerEdge");
	WMSetSliderValue(panel->hceS, value < 0 ? 2 : WMIN(value, 10));
	edgeCallback(NULL, panel);
	WMSetButtonSelected(panel->hcB, GetBoolForKey("HotCorners"));

	for (int i = 0; i < wlengthof(panel->hcactionsT); i++)
		WMSetTextFieldText(panel->hcactionsT[i], NULL);
	array = GetObjectForKey("HotCornerActions");
	if (array) {
		if (!WMIsPLArray(array) || WMGetPropListItemCount(array) != wlengthof(panel->hcactionsT)) {
			wwarning(_("invalid data in option HotCornerActions."));
			return;
		}
		for (int i = 0; i < wlengthof(panel->hcactionsT); i++)
			if (!WMIsPLString(WMGetFromPLArray(array, i))) {
				wwarning(_("invalid data in option HotCornerActions."));
				return;
			}
		for (int i = 0; i < wlengthof(panel->hcactionsT); i++) {
			char *action = WMGetFromPLString(WMGetFromPLArray(array, i));

			if (strcasecmp(action, "None") != 0)
				WMSetTextFieldText(panel->hcactionsT[i], action);
		}
	}
}

static void storeData(_Panel *panel)
{
	WMPropList *list, *item;
	char *text;
	int value;

	SetBoolForKey(WMGetButtonSelected(panel->hcB), "HotCorners");
	text = WMGetTextFieldText(panel->hcdelayT);
	if (sscanf(text, "%i", &value) != 1)
		value = 0;
	SetIntegerForKey(value, "HotCornerDelay");
	wfree(text);
	SetIntegerForKey(WMGetSliderValue(panel->hceS), "HotCornerEdge");

	list = WMCreatePLArray(NULL, NULL);
	for (int i = 0; i < wlengthof(panel->hcactionsT); i++) {
		text = WMGetTextFieldText(panel->hcactionsT[i]);
		item = WMCreatePLString(*text ? text : "None");
		wfree(text);
		WMAddToPLArray(list, item);
		WMReleasePropList(item);
	}
	SetObjectForKey(list, "HotCornerActions");
	WMReleasePropList(list);
}

static void pushDelayButton(WMWidget *w, void *data)
{
	_Panel *panel = data;
	char buffer[32];

	for (int i = 0; i < wlengthof(delayValues); i++)
		if (w == panel->hcdelayB[i]) {
			snprintf(buffer, sizeof(buffer), "%i", delayValues[i]);
			WMSetTextFieldText(panel->hcdelayT, buffer);
			panel->hotcornerDelaySelected = True;
			break;
		}
}

static void delayTextChanged(void *observerData, WMNotification *notification)
{
	_Panel *panel = observerData;

	(void)notification;
	if (panel->hotcornerDelaySelected) {
		for (int i = 0; i < wlengthof(panel->hcdelayB); i++)
			WMSetButtonSelected(panel->hcdelayB[i], False);
		panel->hotcornerDelaySelected = False;
	}
}

static void setCornerImage(WMLabel *label, WMFont *font, RImage *source, int corner)
{
	RImage *image, *scaled;
	WMPixmap *pixmap;
	RColor background = WMGetRColorFromColor(WMGetWidgetBackgroundColor(label));
	int width = source->width / 2, height = source->height / 2;
	int unit = WMAX(17, WMFontHeight(font));

	image = RGetSubImage(source, (corner % 2) * width, (corner / 2) * height, width, height);
	if (!image)
		goto failed;
	if (unit > 17) {
		RCombineImageWithColor(image, &background);
		scaled = RSmoothScaleImage(image, width * unit / 17, height * unit / 17);
		RReleaseImage(image);
		image = scaled;
		if (!image)
			goto failed;
	}
	pixmap = WMCreatePixmapFromRImage(WMWidgetScreen(label), image, 127);
	RReleaseImage(image);
	if (!pixmap)
		goto failed;
	WMSetLabelImage(label, pixmap);
	WMReleasePixmap(pixmap);
	return;
failed:
	wwarning(_("could not resize preference preview: %s"), RMessageForError(RErrorCode));
}

static void createPanel(Panel *p)
{
	_Panel *panel = (_Panel *)p;
	WMScreen *screen = WMWidgetScreen(panel->parent);
	WMFont *font = WMDefaultSystemFont(screen);
	WMColor *color = WMDarkGrayColor(screen);
	WPrefsGroup enable, edge, instructions, delay, actions;
	WMBox *columns, *left, *right, *row;
	WMSize labelSize, size;
	int unit = WMAX(17, WMFontHeight(font));
	char *path;
	RImage *image = NULL;

	panel->box = WPrefsCreatePanelBox(panel->parent);
	columns = WMCreateBox(panel->box);
	WMSetBoxHorizontal(columns, True);
	left = WMCreateBox(columns);
	right = WMCreateBox(columns);

	enable = WPrefsCreateGroup(left, "");
	panel->hcF = enable.frame;
	panel->hcB = WMCreateSwitchButton(enable.content);
	WMSetButtonText(panel->hcB, _("Enable Hot Corners"));
	WPrefsPackButton(enable.content, panel->hcB, 0);
	WPrefsPackGroup(left, &enable, False, 12);

	edge = WPrefsCreateGroup(left, _("Hot Corner Edge"));
	panel->hceF = edge.frame;
	row = WMCreateBox(edge.content);
	WMSetBoxHorizontal(row, True);
	panel->hceS = WMCreateSlider(row);
	WMSetSliderMinValue(panel->hceS, 2);
	WMSetSliderMaxValue(panel->hceS, 10);
	WMSetSliderAction(panel->hceS, edgeCallback, panel);
	WMPackBoxSubview(row, panel->hceS, wmksize(100 * unit / 17, 20 * unit / 17), True, 10);
	panel->hceL = WMCreateLabel(row);
	labelSize = wmksize(1, 1);
	for (int i = 2; i <= 10; i++) {
		WMSetSliderValue(panel->hceS, i);
		edgeCallback(NULL, panel);
		size = WMGetLabelPreferredSize(panel->hceL);
		labelSize.width = WMAX(labelSize.width, size.width);
		labelSize.height = WMAX(labelSize.height, size.height);
	}
	WMPackBoxSubview(row, panel->hceL, labelSize, False, 0);
	WMPackBoxSubview(edge.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);
	WPrefsPackGroup(left, &edge, False, 12);

	instructions = WPrefsCreateGroup(left, "");
	panel->hcdescF = instructions.frame;
	panel->hcdescL = WMCreateLabel(instructions.content);
	WMSetLabelWraps(panel->hcdescL, False);
	WMSetLabelText(panel->hcdescL, _("Instructions:\n\n"
					" - assign command to corner\n"
					" - or leave it empty\n"));
	WPrefsPackLabel(instructions.content, panel->hcdescL, 0);
	row = WMCreateBox(instructions.content);
	WMPackBoxSubview(instructions.content, row, wmksize(1, 1), True, 0);
	WPrefsPackGroup(left, &instructions, True, 0);

	delay = WPrefsCreateGroup(right, _("Hot Corner Delay"));
	panel->hcdelayF = delay.frame;
	row = WMCreateBox(delay.content);
	WMSetBoxHorizontal(row, True);
	for (int i = 0; i < wlengthof(panel->hcdelayB); i++) {
		panel->hcdelayB[i] = WMCreateCustomButton(row, WBBStateChangeMask);
		WPrefsSetClockButtonFont(panel->hcdelayB[i], font, i);
		WMSetButtonAction(panel->hcdelayB[i], pushDelayButton, panel);
		if (i)
			WMGroupButtons(panel->hcdelayB[0], panel->hcdelayB[i]);
		WMPackBoxSubview(row, panel->hcdelayB[i], WMGetViewSize(WMWidgetView(panel->hcdelayB[i])),
				 False, i == 4 ? 12 : 2);
	}
	panel->hcdelayT = WMCreateTextField(row);
	WMPackBoxSubview(row, panel->hcdelayT, WMGetTextFieldPreferredSize(panel->hcdelayT, 5), False, 8);
	WMAddNotificationObserver(delayTextChanged, panel, WMTextDidChangeNotification, panel->hcdelayT);
	panel->hcdelayL = WMCreateLabel(row);
	WMSetLabelText(panel->hcdelayL, _("ms"));
	WMSetLabelTextColor(panel->hcdelayL, color);
	WPrefsPackLabel(row, panel->hcdelayL, 0);
	WMPackBoxSubview(delay.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);
	WPrefsPackGroup(right, &delay, False, 12);

	actions = WPrefsCreateGroup(right, _("Hot Corner Actions"));
	panel->hcactionsF = actions.frame;
	path = LocateImage(ICON_FILE);
	if (path) {
		image = RLoadImage(WMScreenRContext(screen), path, 0);
		if (!image)
			wwarning(_("could not load image file %s"), path);
		wfree(path);
	}
	for (int i = 0; i < wlengthof(panel->hcactionsT); i++) {
		row = WMCreateBox(actions.content);
		WMSetBoxHorizontal(row, True);
		panel->icornerL[i] = WMCreateLabel(row);
		WMSetLabelImagePosition(panel->icornerL[i], WIPImageOnly);
		if (image)
			setCornerImage(panel->icornerL[i], font, image, i);
		size = WMGetLabelPreferredSize(panel->icornerL[i]);
		size.width = WMAX(size.width, 24 * unit / 17);
		size.height = WMAX(size.height, 24 * unit / 17);
		WMPackBoxSubview(row, panel->icornerL[i], size, False, 10);
		panel->hcactionsT[i] = WMCreateTextField(row);
		WMPackBoxSubview(row, panel->hcactionsT[i], WMGetTextFieldPreferredSize(panel->hcactionsT[i], 24),
				 True, 0);
		WMPackBoxSubview(actions.content, row, WMGetBoxMinimumSize(row), False, i == 3 ? 0 : 8);
		WMMapSubwidgets(row);
	}
	if (image)
		RReleaseImage(image);
	row = WMCreateBox(actions.content);
	WMPackBoxSubview(actions.content, row, wmksize(1, 1), True, 0);
	WPrefsPackGroup(right, &actions, True, 0);
	WMPackBoxSubview(columns, left, WMGetBoxMinimumSize(left), True, 12);
	WMPackBoxSubview(columns, right, WMGetBoxMinimumSize(right), True, 0);
	WMPackBoxSubview(panel->box, columns, WMGetBoxMinimumSize(columns), True, 0);
	WMMapSubwidgets(left);
	WMMapSubwidgets(right);
	WMMapSubwidgets(columns);
	WMReleaseColor(color);
	WMReleaseFont(font);
	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);
	showData(panel);
}

Panel *InitHotCornerShortcuts(WMWidget *parent)
{
	_Panel *panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Hot Corner Shortcut Preferences");
	panel->description = _("Choose actions to perform when you move the\n"
			       "mouse pointer to the screen corners.");
	panel->parent = parent;
	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;
	AddSection(panel, ICON_FILE);
	return panel;
}
