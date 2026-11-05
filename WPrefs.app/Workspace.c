/* Workspace.c- workspace options
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

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMButton *linkB;
	WMButton *cyclB;
	WMButton *newB;
	WMPopUpButton *posP;
} _Panel;

#define ICON_FILE	"workspace"

#define DONT_LINK_FILE	"dontlinkworkspaces"
#define CYCLE_FILE	"cycleworkspaces"
#define ADVANCE_FILE	"advancetonewworkspace"
#define WSNAME_FILE 	"workspacename"

static char *WSNamePositions[] = {
	"none",
	"center",
	"top",
	"bottom",
	"topleft",
	"topright",
	"bottomleft",
	"bottomright"
};

static void showData(_Panel * panel)
{
	int i, idx;
	char *str;

	WMSetButtonSelected(panel->linkB, !GetBoolForKey("DontLinkWorkspaces"));

	WMSetButtonSelected(panel->cyclB, GetBoolForKey("CycleWorkspaces"));

	WMSetButtonSelected(panel->newB, GetBoolForKey("AdvanceToNewWorkspace"));

	str = GetStringForKey("WorkspaceNameDisplayPosition");
	if (!str)
		str = "center";

	idx = 1;		/* center */
	for (i = 0; i < wlengthof(WSNamePositions); i++) {
		if (strcasecmp(WSNamePositions[i], str) == 0) {
			idx = i;
			break;
		}
	}
	WMSetPopUpButtonSelectedItem(panel->posP, idx);
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WPrefsGroup navigation;
	struct {
		const char *image;
		const char *text;
		WMButton **button;
	} options[] = {
		{ CYCLE_FILE, _("Wrap to the first workspace from the last workspace"), &panel->cyclB },
		{ DONT_LINK_FILE, _("Switch workspaces while dragging windows"), &panel->linkB },
		{ ADVANCE_FILE, _("Automatically create new workspaces"), &panel->newB },
		{ WSNAME_FILE, _("Position of workspace name display"), NULL }
	};
	const char *positions[] = {
		_("Disable"), _("Center"), _("Top"), _("Bottom"), _("Top/Left"),
		_("Top/Right"), _("Bottom/Left"), _("Bottom/Right")
	};
	int i, j;

	panel->box = WPrefsCreatePanelBox(panel->parent);
	navigation = WPrefsCreateGroup(panel->box, _("Workspace Navigation"));
	for (i = 0; i < wlengthof(options); i++) {
		WMBox *row = WMCreateBox(navigation.content);
		WMLabel *icon = WMCreateLabel(row);
		WMPixmap *image;
		WMSize iconSize;

		WMSetBoxHorizontal(row, True);
		WMSetLabelImagePosition(icon, WIPImageOnly);
		CreateImages(scr, WMScreenRContext(scr), NULL, options[i].image, &image, NULL);
		if (image) {
			WMSetLabelImage(icon, image);
			WMReleasePixmap(image);
		}
		iconSize = WMGetLabelPreferredSize(icon);
		iconSize.width = WMAX(60, iconSize.width);
		WMPackBoxSubview(row, icon, iconSize, False, 8);
		if (options[i].button) {
			WMButton *button = WMCreateSwitchButton(row);

			*options[i].button = button;
			WMSetButtonText(button, options[i].text);
			WPrefsPackButton(row, button, 0);
		} else {
			WMLabel *label = WMCreateLabel(row);

			WMSetLabelText(label, options[i].text);
			WPrefsPackLabel(row, label, 12);
			panel->posP = WMCreatePopUpButton(row);
			for (j = 0; j < wlengthof(positions); j++)
				WMAddPopUpButtonItem(panel->posP, positions[j]);
			WMPackBoxSubview(row, panel->posP, WMGetPopUpButtonPreferredSize(panel->posP), False, 0);
		}
		WMPackBoxSubview(navigation.content, row, WMGetBoxMinimumSize(row), False,
				 i == wlengthof(options) - 1 ? 0 : 10);
		WMMapSubwidgets(row);
	}
	WPrefsPackGroup(panel->box, &navigation, True, 0);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

static void storeData(_Panel * panel)
{
	int tmp = WMGetPopUpButtonSelectedItem(panel->posP);

	if (tmp < 0)
		return;
	SetBoolForKey(!WMGetButtonSelected(panel->linkB), "DontLinkWorkspaces");
	SetBoolForKey(WMGetButtonSelected(panel->cyclB), "CycleWorkspaces");
	SetBoolForKey(WMGetButtonSelected(panel->newB), "AdvanceToNewWorkspace");

	SetStringForKey(WSNamePositions[tmp], "WorkspaceNameDisplayPosition");
}

Panel *InitWorkspace(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Workspace Preferences");

	panel->description = _("Workspace navigation features\n"
			       "and workspace name display settings.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
