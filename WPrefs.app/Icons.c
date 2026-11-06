/* Icons.c- icon preferences
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


static const struct {
	const char *db_value;
	const char *label;
} icon_animation[] = {
	{ "zoom",  N_("Shrinking/Zooming") },
	{ "twist", N_("Spinning/Twisting") },
	{ "flip",  N_("3D-flipping") },
	{ "none",  N_("None") }
};

/*
 * The code is using a convenient trick to make the link between the icon
 * position and its representing number:
 *   bit[0] tell if the icon are arranged horizontally or vertically
 *   bit[2:1] tell the corner to which they are starting from:
 *     bit[2] is for Top or Bottom choice
 *     bit[1] is for Left or Right choice
 */
static const char icon_position_dbvalue[][4] = {
	/* 000: */ "tlv",
	/* 001: */ "tlh",
	/* 010: */ "trv",
	/* 011: */ "trh",
	/* 100: */ "blv",
	/* 101: */ "blh",
	/* 110: */ "brv",
	/* 111: */ "brh"
};

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMFrame *posF;
	WPrefsIconPositionControl position;

	WMFrame *animF;
	WMPopUpButton *animP;

	WMFrame *optF;
	WMButton *arrB;
	WMButton *omnB;
	WMButton *sclB;
	WMButton *marginB;

	struct {
		WMFrame *frame;
		WMSlider *slider;
		WMLabel *label;
	} minipreview;

	WMFrame *sizeF;
	WMPopUpButton *sizeP;

} _Panel;

/*
 * Minimum size for a Mini-Preview:
 * This value is actually twice the size of the minimum icon size choosable.
 * We set the slider min to taht number minus one, because when set to this
 * value WPrefs will consider that the user wants the feature turned off.
 */
static const int minipreview_minimum_size = 2 * 24 - 1;

static const int minipreview_maximum_size = 512;	/* Arbitrary limit for the slider */

#define ICON_FILE	"iconprefs"

static void minipreview_slider_changed(WMWidget *w, void *data)
{
	_Panel *panel = (_Panel *) data;
	char buffer[64];
	int value;

	/* Parameter is not used, but tell the compiler that it is ok */
	(void) w;

	value = WMGetSliderValue(panel->minipreview.slider);

	/* Round the value to a multiple of 8 because it makes the displayed value look better */
	value &= ~7;

	if (value <= minipreview_minimum_size)
		sprintf(buffer, _("OFF"));
	else
		sprintf(buffer, "%i", value);

	WMSetLabelText(panel->minipreview.label, buffer);
}

static void showData(_Panel * panel)
{
	int i;
	char *str;
	Bool b;

	WMSetButtonSelected(panel->arrB, GetBoolForKey("AutoArrangeIcons"));
	WMSetButtonSelected(panel->omnB, GetBoolForKey("StickyIcons"));
	WMSetButtonSelected(panel->sclB, GetBoolForKey("SingleClickLaunch"));
	WMSetButtonSelected(panel->marginB, GetBoolForKey("EnforceIconMargin"));

	str = GetStringForKey("IconPosition");
	if (str != NULL) {
		for (i = 0; i < wlengthof(icon_position_dbvalue); i++)
			if (strcmp(str, icon_position_dbvalue[i]) == 0) {
				panel->position.position = i;
				goto found_position_value;
			}
		wwarning(_("bad value \"%s\" for option %s, using default \"%s\""),
		         str, "IconPosition", icon_position_dbvalue[5]);
	}
	panel->position.position = 5;
 found_position_value:
	WPrefsSetIconPositionControlValue(&panel->position, panel->position.position);

	i = GetIntegerForKey("IconSize");
	i = (i - 24) / 8;

	if (i < 0)
		i = 0;
	else if (i > 29)
		i = 29;
	WMSetPopUpButtonSelectedItem(panel->sizeP, i);

	/* Mini-Previews for Icons */

	str = GetStringForKey("MiniwindowPreviewBalloons");
	if (str != NULL) {
		/* New names found, use them in priority */
		b = GetBoolForKey("MiniwindowPreviewBalloons");
		if (b) {
			i = GetIntegerForKey("MiniPreviewSize");
			if (i <= minipreview_minimum_size)
				i = minipreview_minimum_size;
		} else {
			i = minipreview_minimum_size;
		}
	}
	WMSetSliderValue(panel->minipreview.slider, i);
	minipreview_slider_changed(panel->minipreview.slider, panel);

	/* Animation */
	str = GetStringForKey("IconificationStyle");
	if (str != NULL) {
		for (i = 0; i < wlengthof(icon_animation); i++) {
			if (strcasecmp(str, icon_animation[i].db_value) == 0) {
				WMSetPopUpButtonSelectedItem(panel->animP, i);
				goto found_animation_value;
			}
		}
		wwarning(_("animation style \"%s\" is unknown, resetting to \"%s\""),
		         str, icon_animation[0].db_value);
	}
	/* If we're here, no valid value have been found so we fall-back to the default */
	WMSetPopUpButtonSelectedItem(panel->animP, 0);
 found_animation_value:
	;
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMFont *font = WMDefaultSystemFont(scr);
	WMBox *columns, *left, *right, *row, *sliderRow;
	WPrefsGroup position, size, preview, animation, options;
	WMSize labelSize = wmksize(0, 0), current;
	int i;
	char buf[16];

	panel->box = WPrefsCreatePanelBox(panel->parent);
	columns = WMCreateBox(panel->box);
	WMSetBoxHorizontal(columns, True);
	left = WMCreateBox(columns);
	right = WMCreateBox(columns);

    /***************** Positioning of Icons *****************/
	position = WPrefsCreateGroup(left, _("Icon Positioning"));
	panel->posF = position.frame;
	WPrefsCreateIconPositionControl(&panel->position, position.content, font);
	WMPackBoxSubview(position.content, panel->position.box,
			 WMGetViewSize(WMWidgetView(panel->position.box)), True, 0);
	WPrefsPackGroup(left, &position, True, 12);
	row = WMCreateBox(left);
	WMSetBoxHorizontal(row, True);

    /***************** Icon Size ****************/
	size = WPrefsCreateGroup(row, _("Icon Size"));
	panel->sizeF = size.frame;

	WMSetBalloonTextForView(_("The size of the dock/application icon and miniwindows"),
				WMWidgetView(panel->sizeF));

	panel->sizeP = WMCreatePopUpButton(size.content);
	for (i = 24; i <= 256; i += 8) {
		sprintf(buf, "%ix%i", i, i);
		WMAddPopUpButtonItem(panel->sizeP, buf);
	}

	WMPackBoxSubview(size.content, panel->sizeP, WMGetPopUpButtonPreferredSize(panel->sizeP), False, 0);
	WPrefsPackGroup(row, &size, True, 12);

	/***************** Mini-Previews ****************/
	preview = WPrefsCreateGroup(row, _("Mini-Previews for Icons"));
	panel->minipreview.frame = preview.frame;

	WMSetBalloonTextForView(_("The Mini-Preview provides a small view of the content of the\n"
	                          "window when the mouse is placed over the icon."),
	                        WMWidgetView(panel->minipreview.frame));

	sliderRow = WMCreateBox(preview.content);
	WMSetBoxHorizontal(sliderRow, True);
	panel->minipreview.slider = WMCreateSlider(sliderRow);
	WMSetSliderFont(panel->minipreview.slider, font);
	WMSetSliderMinValue(panel->minipreview.slider, minipreview_minimum_size);
	WMSetSliderMaxValue(panel->minipreview.slider, minipreview_maximum_size);
	WMSetSliderAction(panel->minipreview.slider, minipreview_slider_changed, panel);

	panel->minipreview.label = WMCreateLabel(sliderRow);
	for (i = minipreview_minimum_size; i <= minipreview_maximum_size; i++) {
		WMSetSliderValue(panel->minipreview.slider, i);
		minipreview_slider_changed(NULL, panel);
		current = WMGetLabelPreferredSize(panel->minipreview.label);
		labelSize.width = WMAX(labelSize.width, current.width);
		labelSize.height = WMAX(labelSize.height, current.height);
	}
	WMPackBoxSubview(sliderRow, panel->minipreview.slider,
			 WMGetSliderPreferredSize(panel->minipreview.slider), True, 8);
	WMPackBoxSubview(sliderRow, panel->minipreview.label, labelSize, False, 0);
	WMPackBoxSubview(preview.content, sliderRow, WMGetBoxMinimumSize(sliderRow), False, 0);
	WMMapSubwidgets(sliderRow);
	WPrefsPackGroup(row, &preview, True, 0);
	WMPackBoxSubview(left, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);

    /***************** Animation ****************/
	animation = WPrefsCreateGroup(right, _("Iconification Animation"));
	panel->animF = animation.frame;

	panel->animP = WMCreatePopUpButton(animation.content);
	for (i = 0; i < wlengthof(icon_animation); i++)
		WMAddPopUpButtonItem(panel->animP, _(icon_animation[i].label));

	WMPackBoxSubview(animation.content, panel->animP, WMGetPopUpButtonPreferredSize(panel->animP), False, 0);
	WPrefsPackGroup(right, &animation, False, 12);

    /***************** Options ****************/
	options = WPrefsCreateGroup(right, "");
	panel->optF = options.frame;

	panel->arrB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->arrB, _("Auto-arrange icons"));

	WMSetBalloonTextForView(_("Keep icons and miniwindows arranged all the time."), WMWidgetView(panel->arrB));

	panel->omnB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->omnB, _("Omnipresent miniwindows"));

	WMSetBalloonTextForView(_("Make miniwindows be present in all workspaces."), WMWidgetView(panel->omnB));

	panel->sclB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->sclB, _("Single click activation"));

	WMSetBalloonTextForView(_("Launch applications and restore windows with a single click."), WMWidgetView(panel->sclB));

	panel->marginB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->marginB, _("Enforce icon margin"));

	WMSetBalloonTextForView(_("Make sure that the icon image does not protrude into the icon frame."), WMWidgetView(panel->marginB));

	WPrefsPackButton(options.content, panel->arrB, 8);
	WPrefsPackButton(options.content, panel->omnB, 8);
	WPrefsPackButton(options.content, panel->sclB, 8);
	WPrefsPackButton(options.content, panel->marginB, 0);
	WPrefsPackGroup(right, &options, True, 0);
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

static void storeData(_Panel * panel)
{
	int i;

	SetBoolForKey(WMGetButtonSelected(panel->arrB), "AutoArrangeIcons");
	SetBoolForKey(WMGetButtonSelected(panel->omnB), "StickyIcons");
	SetBoolForKey(WMGetButtonSelected(panel->sclB), "SingleClickLaunch");
	SetBoolForKey(WMGetButtonSelected(panel->marginB), "EnforceIconMargin");

	SetIntegerForKey(WMGetPopUpButtonSelectedItem(panel->sizeP) * 8 + 24, "IconSize");

	SetStringForKey(icon_position_dbvalue[panel->position.position], "IconPosition");

	i = WMGetSliderValue(panel->minipreview.slider);
	if (i <= minipreview_minimum_size) {
		SetBoolForKey(False, "MiniwindowPreviewBalloons");
	} else {
		SetBoolForKey(True, "MiniwindowPreviewBalloons");
		if (i < minipreview_maximum_size) {
			/*
			 * If the value is bigger, it means it was edited by the user manually
			 * so we keep as-is. Otherwise, we round it to a multiple of 8 like it
			 * was done for display
			 */
			i &= ~7;
		}
		SetIntegerForKey(i, "MiniPreviewSize");
	}

	SetStringForKey(icon_animation[WMGetPopUpButtonSelectedItem(panel->animP)].db_value, "IconificationStyle");
}

Panel *InitIcons(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Icon Preferences");

	panel->description = _("Icon/Miniwindow handling options. Icon positioning\n"
			       "area, sizes of icons, miniaturization animation style.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
