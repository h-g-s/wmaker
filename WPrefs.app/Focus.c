/* Focus.c- input and colormap focus stuff
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
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
#include "FontControls.h"

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMFrame *kfocF;
	WMButton *kfocB[2];

	WMFrame *cfocF;
	WMButton *autB;
	WMButton *manB;

	WMFrame *raisF;
	WMButton *raisB[5];
	WMTextField *raisT;
	WMLabel *raisL;

	WMFrame *optF;
	WMButton *ignB;
	WMButton *newB;
	WMButton *craisB;

	char raiseDelaySelected;
} _Panel;

#define ICON_FILE	"windowfocus"

static void showData(_Panel * panel)
{
	char *str;
	int i;
	char buffer[32];

	str = GetStringForKey("FocusMode");
	if (!str)
		str = "manual";
	if (strcasecmp(str, "manual") == 0 || strcasecmp(str, "clicktofocus") == 0)
		WMSetButtonSelected(panel->kfocB[0], 1);
	else if (strcasecmp(str, "auto") == 0 || strcasecmp(str, "semiauto") == 0
		 || strcasecmp(str, "sloppy") == 0)
		WMSetButtonSelected(panel->kfocB[1], 1);
	else {
		wwarning(_("bad option value %s for option FocusMode. Using default Manual"), str);
		WMSetButtonSelected(panel->kfocB[0], 1);
	}

	 /**/ str = GetStringForKey("ColormapMode");
	if (!str)
		str = "auto";
	if (strcasecmp(str, "manual") == 0 || strcasecmp(str, "clicktofocus") == 0) {
		WMPerformButtonClick(panel->manB);
	} else if (strcasecmp(str, "auto") == 0 || strcasecmp(str, "focusfollowsmouse") == 0) {
		WMPerformButtonClick(panel->autB);
	} else {
		wwarning(_("bad option value %s for option ColormapMode. Using default Auto"), str);
		WMPerformButtonClick(panel->manB);
	}

	 /**/ i = GetIntegerForKey("RaiseDelay");
	sprintf(buffer, "%i", i);
	WMSetTextFieldText(panel->raisT, buffer);

	switch (i) {
	case 0:
		WMPerformButtonClick(panel->raisB[0]);
		break;
	case 10:
		WMPerformButtonClick(panel->raisB[1]);
		break;
	case 100:
		WMPerformButtonClick(panel->raisB[2]);
		break;
	case 350:
		WMPerformButtonClick(panel->raisB[3]);
		break;
	case 800:
		WMPerformButtonClick(panel->raisB[4]);
		break;
	}

	 /**/ WMSetButtonSelected(panel->ignB, GetBoolForKey("IgnoreFocusClick"));

	WMSetButtonSelected(panel->newB, GetBoolForKey("AutoFocus"));
	WMSetButtonSelected(panel->craisB, GetBoolForKey("CirculateRaise"));
}

static void storeData(_Panel * panel)
{
	char *str;
	int i;

	if (WMGetButtonSelected(panel->kfocB[1]))
		str = "sloppy";
	else
		str = "manual";

	SetStringForKey(str, "FocusMode");

	if (WMGetButtonSelected(panel->manB)) {
		SetStringForKey("manual", "ColormapMode");
	} else {
		SetStringForKey("auto", "ColormapMode");
	}

	str = WMGetTextFieldText(panel->raisT);
	if (sscanf(str, "%i", &i) != 1)
		i = 0;
	SetIntegerForKey(i, "RaiseDelay");
	free(str);

	SetBoolForKey(WMGetButtonSelected(panel->ignB), "IgnoreFocusClick");
	SetBoolForKey(WMGetButtonSelected(panel->newB), "AutoFocus");
	SetBoolForKey(WMGetButtonSelected(panel->craisB), "CirculateRaise");
}

static void pushDelayButton(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;

	panel->raiseDelaySelected = 1;
	if (w == panel->raisB[0]) {
		WMSetTextFieldText(panel->raisT, _("OFF"));
	} else if (w == panel->raisB[1]) {
		WMSetTextFieldText(panel->raisT, "10");
	} else if (w == panel->raisB[2]) {
		WMSetTextFieldText(panel->raisT, "100");
	} else if (w == panel->raisB[3]) {
		WMSetTextFieldText(panel->raisT, "350");
	} else if (w == panel->raisB[4]) {
		WMSetTextFieldText(panel->raisT, "800");
	}
}

static void raiseTextChanged(void *observerData, WMNotification * notification)
{
	_Panel *panel = (_Panel *) observerData;
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) notification;

	if (panel->raiseDelaySelected) {
		for (i = 0; i < 5; i++) {
			WMSetButtonSelected(panel->raisB[i], False);
		}
		panel->raiseDelaySelected = 0;
	}
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMFont *font = WMDefaultSystemFont(scr);
	WMColor *color = WMDarkGrayColor(scr);
	WPrefsGroup input, colormap, delay, options;
	WMBox *columns, *left, *right, *row;
	int choiceWidth = 226 * WMAX(17, WMFontHeight(font)) / 17;
	int i;

	panel->box = WPrefsCreatePanelBox(panel->parent);
	columns = WMCreateBox(panel->box);
	WMSetBoxHorizontal(columns, True);
	left = WMCreateBox(columns);
	right = WMCreateBox(columns);

    /***************** Input Focus Mode *****************/
	input = WPrefsCreateGroup(left, _("Input Focus Mode"));
	panel->kfocF = input.frame;
	panel->kfocB[0] = WMCreateRadioButton(input.content);
	WMSetButtonText(panel->kfocB[0], _("Manual:  Click on the window to set "
	                                   "keyboard input focus"));
	panel->kfocB[1] = WMCreateRadioButton(input.content);
	WMGroupButtons(panel->kfocB[0], panel->kfocB[1]);
	WMSetButtonText(panel->kfocB[1], _("Auto:  Set keyboard input focus to "
	                                   "the window under the mouse pointer"));
	for (i = 0; i < 2; i++)
		WMPackBoxSubview(input.content, panel->kfocB[i],
				 WMGetButtonPreferredSizeForWidth(panel->kfocB[i], choiceWidth), False, i ? 0 : 8);
	WPrefsPackGroup(left, &input, False, 12);

    /***************** Colormap Installation Mode ****************/

	colormap = WPrefsCreateGroup(left, _("Install colormap from the window..."));
	panel->cfocF = colormap.frame;

	WMSetBalloonTextForView(_("This option is for screens that can display only a limited number\n"
	                          "of colors at a time, so they use an indexed table of colors (called\n"
	                          "a ColorMap) that each application customizes for its needs, and\n"
	                          "WindowMaker will set the global ColorMap dynamically from the\n"
	                          "active application.\n"
	                          "You can know the capability of your screen in WindowMaker's info\n"
	                          "panel as the 'visual'."),
	                        WMWidgetView(panel->cfocF));

	panel->manB = WMCreateRadioButton(colormap.content);
	WMSetButtonText(panel->manB, _("...that has the input focus"));

	panel->autB = WMCreateRadioButton(colormap.content);
	WMSetButtonText(panel->autB, _("...that's under the mouse pointer"));
	WMGroupButtons(panel->manB, panel->autB);

	WMPackBoxSubview(colormap.content, panel->manB,
			 WMGetButtonPreferredSizeForWidth(panel->manB, choiceWidth), False, 8);
	WMPackBoxSubview(colormap.content, panel->autB,
			 WMGetButtonPreferredSizeForWidth(panel->autB, choiceWidth), False, 0);
	WPrefsPackGroup(left, &colormap, True, 0);

    /***************** Automatic window raise delay *****************/
	delay = WPrefsCreateGroup(right, _("Automatic Window Raise Delay"));
	panel->raisF = delay.frame;
	row = WMCreateBox(delay.content);
	WMSetBoxHorizontal(row, True);

	for (i = 0; i < 5; i++) {
		panel->raisB[i] = WMCreateCustomButton(row, WBBStateChangeMask);
		WPrefsSetClockButtonFont(panel->raisB[i], font, i);
		WMSetButtonAction(panel->raisB[i], pushDelayButton, panel);
		if (i > 0)
			WMGroupButtons(panel->raisB[0], panel->raisB[i]);
		WMPackBoxSubview(row, panel->raisB[i], WMGetViewSize(WMWidgetView(panel->raisB[i])),
				 False, i == 4 ? 12 : 2);
	}

	panel->raisT = WMCreateTextField(row);
	WMPackBoxSubview(row, panel->raisT, WMGetTextFieldPreferredSize(panel->raisT, 5), False, 8);
	WMAddNotificationObserver(raiseTextChanged, panel, WMTextDidChangeNotification, panel->raisT);

	panel->raisL = WMCreateLabel(row);
	WMSetLabelText(panel->raisL, _("ms"));
	WMSetLabelTextColor(panel->raisL, color);
	WMSetLabelFont(panel->raisL, font);
	WPrefsPackLabel(row, panel->raisL, 0);
	WMPackBoxSubview(delay.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);
	WPrefsPackGroup(right, &delay, False, 12);

    /***************** Options ****************/
	options = WPrefsCreateGroup(right, "");
	panel->optF = options.frame;

	panel->ignB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->ignB, _("Do not let applications receive the click used to focus windows"));

	panel->newB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->newB, _("Automatically focus new windows"));

	panel->craisB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->craisB, _("Raise window when switching focus with keyboard"));

	WMPackBoxSubview(options.content, panel->ignB,
			 WMGetButtonPreferredSizeForWidth(panel->ignB, choiceWidth), False, 8);
	WMPackBoxSubview(options.content, panel->newB,
			 WMGetButtonPreferredSizeForWidth(panel->newB, choiceWidth), False, 8);
	WMPackBoxSubview(options.content, panel->craisB,
			 WMGetButtonPreferredSizeForWidth(panel->craisB, choiceWidth), False, 0);
	WPrefsPackGroup(right, &options, True, 0);
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

Panel *InitFocus(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Window Focus Preferences");
	panel->description = _("Keyboard focus switching policy and related options.");
	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
