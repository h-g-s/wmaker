/* MenuPreferences.c- menu related preferences
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

	WPrefsSpeedControl speed;

	WMButton *aliyB;
	WMButton *alinB;

	WMButton *autoB;
	WMButton *autoC;
	WMButton *wrapB;

} _Panel;

#define ICON_FILE	"menuprefs"

static void showData(_Panel * panel)
{
	WPrefsSetSpeedControlValue(&panel->speed, GetSpeedForKey("MenuScrollSpeed"));

	if (GetBoolForKey("AlignSubmenus"))
		WMPerformButtonClick(panel->aliyB);
	else
		WMPerformButtonClick(panel->alinB);

	WMSetButtonSelected(panel->wrapB, GetBoolForKey("WrapMenus"));

	WMSetButtonSelected(panel->autoB, GetBoolForKey("ScrollableMenus"));

	WMSetButtonSelected(panel->autoC, GetBoolForKey("ViKeyMenus"));
}

static void storeData(_Panel * panel)
{
	SetSpeedForKey(WPrefsGetSpeedControlValue(&panel->speed), "MenuScrollSpeed");

	SetBoolForKey(WMGetButtonSelected(panel->aliyB), "AlignSubmenus");

	SetBoolForKey(WMGetButtonSelected(panel->wrapB), "WrapMenus");
	SetBoolForKey(WMGetButtonSelected(panel->autoB), "ScrollableMenus");
	SetBoolForKey(WMGetButtonSelected(panel->autoC), "ViKeyMenus");
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMFont *font = WMDefaultSystemFont(scr);
	WPrefsGroup scrolling, alignment, options;
	WMBox *top, *choices;
	WMSize size;

	panel->box = WPrefsCreatePanelBox(panel->parent);
	top = WMCreateBox(panel->box);
	WMSetBoxHorizontal(top, True);
	scrolling = WPrefsCreateGroup(top, _("Menu Scrolling Speed"));
	WPrefsCreateSpeedControl(&panel->speed, scrolling.content, font);
	size = WMGetViewSize(WMWidgetView(panel->speed.box));
	WMPackBoxSubview(scrolling.content, panel->speed.box, size, False, 0);
	WPrefsPackGroup(top, &scrolling, True, 12);

	alignment = WPrefsCreateGroup(top, _("Submenu Alignment"));
	choices = WMCreateBox(alignment.content);
	WMSetBoxHorizontal(choices, True);
	panel->alinB = WMCreateButton(choices, WBTOnOff);
	WPrefsSetMenuAlignmentButtonFont(panel->alinB, font, False);
	panel->aliyB = WMCreateButton(choices, WBTOnOff);
	WPrefsSetMenuAlignmentButtonFont(panel->aliyB, font, True);
	WMGroupButtons(panel->alinB, panel->aliyB);
	WPrefsPackButton(choices, panel->alinB, 12);
	WPrefsPackButton(choices, panel->aliyB, 0);
	WMPackBoxSubview(alignment.content, choices, WMGetBoxMinimumSize(choices), False, 0);
	WPrefsPackGroup(top, &alignment, True, 0);
	WMPackBoxSubview(panel->box, top, WMGetBoxMinimumSize(top), False, 12);
	WMMapSubwidgets(choices);
	WMMapSubwidgets(top);

	options = WPrefsCreateGroup(panel->box, _("Options"));
	panel->wrapB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->wrapB, _("Always open submenus inside the screen, instead of scrolling."));

	panel->autoB = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->autoB, _("Scroll off-screen menus when pointer is moved over them."));
	panel->autoC = WMCreateSwitchButton(options.content);
	WMSetButtonText(panel->autoC, _("Use h/j/k/l keys to select menu options."));

	WPrefsPackButton(options.content, panel->wrapB, 8);
	WPrefsPackButton(options.content, panel->autoB, 8);
	WPrefsPackButton(options.content, panel->autoC, 0);
	WPrefsPackGroup(panel->box, &options, True, 0);
	WMReleaseFont(font);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

Panel *InitMenuPreferences(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Menu Preferences");

	panel->description = _("Menu usability related options. Scrolling speed,\n" "alignment of submenus etc.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
