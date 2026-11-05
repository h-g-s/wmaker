/* Preferences.c- misc ergonomic preferences
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


/* Possible choices to display window information during a resize */
static const struct {
	const char *db_value;
	const char *label;
} resize_display[] = {
	{ "corner",   N_("Corner of screen") },
	{ "center",   N_("Center of screen") },
	{ "floating", N_("Center of resized window") },
	{ "line",     N_("Technical drawing-like") },
	{ "none",     N_("Disabled") }
};

/* Possible choices to display window information while a window is being moved */
static const struct {
	const char *db_value;
	const char *label;
} move_display[] = {
	{ "corner",   N_("Corner of screen") },
	{ "center",   N_("Center of screen") },
	{ "floating", N_("Center of resized window") },
	{ "none",     N_("Disabled") }
};

/* All the places where a balloon can be used to display more stuff to user */
static const struct {
	const char *db_key;
	const char *label;
} balloon_choices[] = {
	{ "WindowTitleBalloons",       N_("incomplete window titles"), },
	{ "MiniwindowTitleBalloons",   N_("miniwindow titles"), },
	{ "AppIconBalloons",           N_("application/dock icons"), },
	{ "HelpBalloons",              N_("internal help"), }
};

static const struct {
	const char *db_key;
	int default_value;
	const char *label;
	const char *balloon_msg;
} appicon_bouncing[] = {
	{ "DoNotMakeAppIconsBounce",   False, N_("Disable AppIcon bounce"),
	  N_("By default, the AppIcon bounces when the application is launched") },

	{ "BounceAppIconsWhenUrgent",  True,  N_("Bounce when the application wants attention"),
	  NULL },

	{ "RaiseAppIconsWhenBouncing", False, N_("Raise AppIcon when bouncing"),
	  N_("Otherwise you will not see it bouncing if\nthere is a window in front of the AppIcon") }
};

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMPopUpButton *sizeP;

	WMPopUpButton *posiP;

	WMButton *ballB[wlengthof_nocheck(balloon_choices)];

	WMButton *bounceB[wlengthof_nocheck(appicon_bouncing)];

	WMSlider *borderS;
	WMLabel *borderL;
	WMButton *lrB;
	WMButton *tbB;

} _Panel;

#define ICON_FILE	"ergonomic"

static void borderCallback(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	char buffer[64];
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	i = WMGetSliderValue(panel->borderS);

	if (i == 0)
		sprintf(buffer, _("OFF"));
	else if (i == 1)
		sprintf(buffer, _("1 pixel"));
	else if (i <= 4)
		/* 2-4 */
		sprintf(buffer, _("%i pixels"), i);
	else
		/* >4 */
		sprintf(buffer, _("%i pixels "), i);	/* note space! */
	WMSetLabelText(panel->borderL, buffer);
}

static void showData(_Panel * panel)
{
	char *str;
	int x;

	str = GetStringForKey("ResizeDisplay");
	if (str != NULL) {
		for (x = 0; x < wlengthof(resize_display); x++) {
			if (strcasecmp(str, resize_display[x].db_value) == 0) {
				WMSetPopUpButtonSelectedItem(panel->sizeP, x);
				goto found_valid_resize_display;
			}
		}
		wwarning(_("bad value \"%s\" for option %s, using default \"%s\""),
		         str, "ResizeDisplay", resize_display[0].db_value);
	}
	WMSetPopUpButtonSelectedItem(panel->sizeP, 0);
 found_valid_resize_display:

	str = GetStringForKey("MoveDisplay");
	if (str != NULL) {
		for (x = 0; x < wlengthof(move_display); x++) {
			if (strcasecmp(str, move_display[x].db_value) == 0) {
				WMSetPopUpButtonSelectedItem(panel->posiP, x);
				goto found_valid_move_display;
			}
		}
		wwarning(_("bad value \"%s\" for option %s, using default \"%s\""),
		         str, "MoveDisplay", move_display[0].db_value);
	}
	WMSetPopUpButtonSelectedItem(panel->posiP, 0);
 found_valid_move_display:

	x = GetIntegerForKey("WorkspaceBorderSize");
	x = x < 0 ? 0 : x;
	x = x > 5 ? 5 : x;
	WMSetSliderValue(panel->borderS, x);
	borderCallback(NULL, panel);

	str = GetStringForKey("WorkspaceBorder");
	if (!str)
		str = "none";
	if (strcasecmp(str, "LeftRight") == 0) {
		WMSetButtonSelected(panel->lrB, True);
	} else if (strcasecmp(str, "TopBottom") == 0) {
		WMSetButtonSelected(panel->tbB, True);
	} else if (strcasecmp(str, "AllDirections") == 0) {
		WMSetButtonSelected(panel->tbB, True);
		WMSetButtonSelected(panel->lrB, True);
	}

	for (x = 0; x < wlengthof(appicon_bouncing); x++) {
		if (GetStringForKey(appicon_bouncing[x].db_key))
			WMSetButtonSelected(panel->bounceB[x], GetBoolForKey(appicon_bouncing[x].db_key));
	}

	for (x = 0; x < wlengthof(balloon_choices); x++)
		WMSetButtonSelected(panel->ballB[x], GetBoolForKey(balloon_choices[x].db_key));
}

static void storeData(_Panel * panel)
{
	char *str;
	Bool lr, tb;
	int i;

	i = WMGetPopUpButtonSelectedItem(panel->sizeP);
	if (i < 0)
		return;
	SetStringForKey(resize_display[i].db_value, "ResizeDisplay");

	i = WMGetPopUpButtonSelectedItem(panel->posiP);
	if (i < 0)
		return;
	SetStringForKey(move_display[i].db_value, "MoveDisplay");

	lr = WMGetButtonSelected(panel->lrB);
	tb = WMGetButtonSelected(panel->tbB);
	if (lr && tb)
		str = "AllDirections";
	else if (lr)
		str = "LeftRight";
	else if (tb)
		str = "TopBottom";
	else
		str = "None";
	SetStringForKey(str, "WorkspaceBorder");
	SetIntegerForKey(WMGetSliderValue(panel->borderS), "WorkspaceBorderSize");

	for (i = 0; i < wlengthof(appicon_bouncing); i++)
		SetBoolForKey(WMGetButtonSelected(panel->bounceB[i]), appicon_bouncing[i].db_key);

	for (i = 0; i < wlengthof(balloon_choices); i++)
		SetBoolForKey(WMGetButtonSelected(panel->ballB[i]), balloon_choices[i].db_key);
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *screen = WMWidgetScreen(panel->parent);
	WMFont *font = WMDefaultSystemFont(screen);
	WMBox *columns, *left, *right, *row;
	WPrefsGroup size, position, balloons, bouncing, border;
	WMSize labelSize = wmksize(0, 0), current;
	int i;

	panel->box = WPrefsCreatePanelBox(panel->parent);
	columns = WMCreateBox(panel->box);
	WMSetBoxHorizontal(columns, True);
	left = WMCreateBox(columns);
	right = WMCreateBox(columns);

	size = WPrefsCreateGroup(left, _("Size Display"));
	WMSetBalloonTextForView(_("The position or style of the window size\n"
				  "display that's shown when a window is resized."), WMWidgetView(size.frame));

	panel->sizeP = WMCreatePopUpButton(size.content);
	for (i = 0; i < wlengthof(resize_display); i++)
		WMAddPopUpButtonItem(panel->sizeP, _(resize_display[i].label));
	WMPackBoxSubview(size.content, panel->sizeP, WMGetPopUpButtonPreferredSize(panel->sizeP), False, 0);
	WPrefsPackGroup(left, &size, False, 12);

	position = WPrefsCreateGroup(left, _("Position Display"));
	WMSetBalloonTextForView(_("The position or style of the window position\n"
				  "display that's shown when a window is moved."), WMWidgetView(position.frame));

	panel->posiP = WMCreatePopUpButton(position.content);
	for (i = 0; i < wlengthof(move_display); i++)
		WMAddPopUpButtonItem(panel->posiP, _(move_display[i].label));
	WMPackBoxSubview(position.content, panel->posiP, WMGetPopUpButtonPreferredSize(panel->posiP), False, 0);
	WPrefsPackGroup(left, &position, False, 12);

	balloons = WPrefsCreateGroup(right, _("Show balloon for..."));
	for (i = 0; i < wlengthof(balloon_choices); i++) {
		panel->ballB[i] = WMCreateSwitchButton(balloons.content);
		WMSetButtonText(panel->ballB[i], _(balloon_choices[i].label));
		WPrefsPackButton(balloons.content, panel->ballB[i], i + 1 == wlengthof(balloon_choices) ? 0 : 8);
	}
	WPrefsPackGroup(right, &balloons, False, 12);

	bouncing = WPrefsCreateGroup(left, _("AppIcon bouncing"));
	for (i = 0; i < wlengthof(appicon_bouncing); i++) {
		panel->bounceB[i] = WMCreateSwitchButton(bouncing.content);
		WMSetButtonText(panel->bounceB[i], _(appicon_bouncing[i].label));
		if (appicon_bouncing[i].default_value)
			WMSetButtonSelected(panel->bounceB[i], True);
		if (appicon_bouncing[i].balloon_msg)
			WMSetBalloonTextForView(_(appicon_bouncing[i].balloon_msg),
			                        WMWidgetView(panel->bounceB[i]));
		WPrefsPackButton(bouncing.content, panel->bounceB[i], i + 1 == wlengthof(appicon_bouncing) ? 0 : 8);
	}
	WPrefsPackGroup(left, &bouncing, True, 0);

	border = WPrefsCreateGroup(right, _("Workspace border"));
	row = WMCreateBox(border.content);
	WMSetBoxHorizontal(row, True);
	panel->borderS = WMCreateSlider(row);
	WMSetSliderFont(panel->borderS, font);
	WMSetSliderMinValue(panel->borderS, 0);
	WMSetSliderMaxValue(panel->borderS, 5);
	WMSetSliderAction(panel->borderS, borderCallback, panel);
	WMPackBoxSubview(row, panel->borderS, WMGetSliderPreferredSize(panel->borderS), True, 12);

	panel->borderL = WMCreateLabel(row);
	for (i = 0; i <= 5; i++) {
		WMSetSliderValue(panel->borderS, i);
		borderCallback(NULL, panel);
		current = WMGetLabelPreferredSize(panel->borderL);
		labelSize.width = WMAX(labelSize.width, current.width);
		labelSize.height = WMAX(labelSize.height, current.height);
	}
	WMPackBoxSubview(row, panel->borderL, labelSize, False, 0);
	WMPackBoxSubview(border.content, row, WMGetBoxMinimumSize(row), False, 8);
	WMMapSubwidgets(row);
	row = WMCreateBox(border.content);
	WMSetBoxHorizontal(row, True);
	panel->lrB = WMCreateSwitchButton(row);
	WMSetButtonText(panel->lrB, _("Left/Right"));
	WPrefsPackButton(row, panel->lrB, 12);
	panel->tbB = WMCreateSwitchButton(row);
	WMSetButtonText(panel->tbB, _("Top/Bottom"));
	WPrefsPackButton(row, panel->tbB, 0);
	WMPackBoxSubview(border.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);
	WPrefsPackGroup(right, &border, True, 0);
	WMPackBoxSubview(columns, left, WMGetBoxMinimumSize(left), True, 12);
	WMPackBoxSubview(columns, right, WMGetBoxMinimumSize(right), True, 0);
	WMPackBoxSubview(panel->box, columns, WMGetBoxMinimumSize(columns), True, 0);
	WMMapSubwidgets(left);
	WMMapSubwidgets(right);
	WMMapSubwidgets(columns);
	WMReleaseFont(font);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

Panel *InitPreferences(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Miscellaneous Ergonomic Preferences");
	panel->description = _("Various settings like balloon text, geometry\n" "displays etc.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
