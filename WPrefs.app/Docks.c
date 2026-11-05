/* Workspace.c- workspace options
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 2012 Daniel Déchelotte (heavily inspired from file (c) Alfredo K. Kojima)
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


static const char *const clip_delay_frame_titles[] = {
	N_("Clip autocollapsing delays"),
	N_("Clip autoraising delays")
};

static const struct {
	const char *key;
	const char *string;
} auto_delay[] = {
	{ "ClipAutoexpandDelay",   N_("Before auto-expansion") },
	{ "ClipAutocollapseDelay", N_("Before auto-collapsing") },
	{ "ClipAutoraiseDelay",    N_("Before auto-raising") },
	{ "ClipAutolowerDelay",    N_("Before auto-lowering") }
};

static char *autoDelayPresetValues[5] = { "0", "100", "250", "600", "1000" };

static const struct {
	const char *disable_key;
	const char *icon_file;
	const char *preview_file;
	const char *balloon_text;
} dock_config[] = {
	{ "DisableDock", "dock", "dock-preview",
	  N_("Disable/enable the application Dock (the\nvertical icon bar in the side of the screen).") },
	{ "DisableClip", "clip", "clip-preview",
	  N_("Disable/enable the Clip (that thing with\na paper clip icon).") },
	{ "DisableDrawers", "drawer", "drawer-preview",
	  N_("Disable/enable Drawers (a dock that stores\napplication icons horizontally). The dock is required.") }
};

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMLabel *autoDelayL[wlengthof_nocheck(auto_delay)];
	WMButton *autoDelayB[wlengthof_nocheck(auto_delay)][wlengthof_nocheck(autoDelayPresetValues)];
	WMTextField *autoDelayT[wlengthof_nocheck(auto_delay)];
	WMLabel *autoDelayMsL[wlengthof_nocheck(auto_delay)];

	WMButton *docksB[wlengthof_nocheck(dock_config)];
} _Panel;

#define ICON_FILE	"dockclipdrawersection"

#define ARQUIVO_XIS	"xis"

static void showData(_Panel *panel);
static void storeData(_Panel *panel);


static void pushAutoDelayButton(WMWidget *w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int i, j;
	for (i = 0; i < wlengthof(auto_delay); i++)
	{
		for (j = 0; j < wlengthof(autoDelayPresetValues); j++)
		{
			if (w == panel->autoDelayB[i][j])
			{
				WMSetTextFieldText(panel->autoDelayT[i], autoDelayPresetValues[j]);
				return;
			}
		}
	}
}

static void adjustButtonSelectionBasedOnValue(_Panel *panel, int row, const char *value)
{
	int j;

	if (!value)
		return;

	for (j = 0; j < wlengthof(autoDelayPresetValues); j++)
	{
		int isThatOne = !strcmp(autoDelayPresetValues[j], value);
		WMSetButtonSelected(panel->autoDelayB[row][j], isThatOne);
		if (isThatOne)
			return;
	}
}

static void autoDelayChanged(void *observerData, WMNotification *notification)
{
	_Panel *panel = (_Panel *) observerData;
	int row;
	WMTextField *anAutoDelayT = (WMTextField *) WMGetNotificationObject(notification);
	for (row = 0; row < wlengthof(panel->autoDelayT); row++)
	{
		if (anAutoDelayT != panel->autoDelayT[row])
		{
			continue;
		}
		char *value = WMGetTextFieldText(anAutoDelayT);
		adjustButtonSelectionBasedOnValue(panel, row, value);
		free(value);
		return;
	}
}

static void pushDockButton(WMWidget *w, void *data)
{
	_Panel *panel = (_Panel *) data;
	WMButton *button = (WMButton *) w;
	if (button == panel->docksB[0] &&
	    !WMGetButtonSelected(panel->docksB[0]))
	{
		WMSetButtonSelected(panel->docksB[2], False);
	}
	if (button == panel->docksB[2] &&
	    WMGetButtonSelected(panel->docksB[2]))
	{
		WMSetButtonSelected(panel->docksB[0], True);
	}
}

static void createPanel(Panel *p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMPixmap *icon1, *icon2;
	RImage *xis = NULL;
	RContext *rc = WMScreenRContext(scr);
	char *path;
	int i, j, k;
	WMFont *font = WMDefaultSystemFont(scr);
	WMColor *color = WMDarkGrayColor(scr);
	WMBox *columns, *delays;
	WPrefsGroup dock;

	path = LocateImage(ARQUIVO_XIS);
	if (path) {
		xis = RLoadImage(rc, path, 0);
		if (!xis) {
			wwarning(_("could not load image file %s"), path);
		}
		wfree(path);
	}
	if (xis && WMFontHeight(font) > 17) {
		RImage *scaled = RSmoothScaleImage(xis, 192, 192);

		if (!scaled)
			wwarning(_("could not scale disabled preview overlay: %s"), RMessageForError(RErrorCode));
		RReleaseImage(xis);
		xis = scaled;
	}

	panel->box = WPrefsCreatePanelBox(panel->parent);
	columns = WMCreateBox(panel->box);
	WMSetBoxHorizontal(columns, True);
	delays = WMCreateBox(columns);

	for (k = 0; k < wlengthof(clip_delay_frame_titles); k++)
	{
		WPrefsGroup group = WPrefsCreateGroup(delays, _(clip_delay_frame_titles[k]));
		WMBox *rows[2];
		WMSize labelSize = wmksize(0, 0);

		for (i = 0; i < 2; i++) {
			WMSize size;
			int index = i + k * 2;

			rows[i] = WMCreateBox(group.content);
			WMSetBoxHorizontal(rows[i], True);
			panel->autoDelayL[index] = WMCreateLabel(rows[i]);
			WMSetLabelText(panel->autoDelayL[index], _(auto_delay[index].string));
			size = WMGetLabelPreferredSize(panel->autoDelayL[index]);
			labelSize.width = WMAX(labelSize.width, size.width);
			labelSize.height = WMAX(labelSize.height, size.height);
		}
		for (i = 0; i < 2; i++)
		{
			int index = i + k * 2;
			WMBox *row = rows[i];

			WMPackBoxSubview(row, panel->autoDelayL[index], labelSize, False, 12);
			for (j = 0; j < wlengthof(autoDelayPresetValues); j++)
			{
				WMButton *button = WMCreateCustomButton(row, WBBStateChangeMask);

				panel->autoDelayB[index][j] = button;
				WPrefsSetClockButtonFont(button, font, j);
				WMSetButtonAction(button, pushAutoDelayButton, panel);
				if (j > 0)
					WMGroupButtons(panel->autoDelayB[index][0], button);
				WMPackBoxSubview(row, button, WMGetViewSize(WMWidgetView(button)), False,
						 j + 1 == wlengthof(autoDelayPresetValues) ? 12 : 2);
			}

			panel->autoDelayT[index] = WMCreateTextField(row);
			WMPackBoxSubview(row, panel->autoDelayT[index],
					 WMGetTextFieldPreferredSize(panel->autoDelayT[index], 5), False, 8);
			WMAddNotificationObserver(autoDelayChanged, panel, WMTextDidChangeNotification,
						  panel->autoDelayT[index]);
			panel->autoDelayMsL[index] = WMCreateLabel(row);
			WMSetLabelText(panel->autoDelayMsL[index], _("ms"));
			WMSetLabelTextColor(panel->autoDelayMsL[index], color);
			WPrefsPackLabel(row, panel->autoDelayMsL[index], 0);
			WMPackBoxSubview(group.content, row, WMGetBoxMinimumSize(row), False, i ? 0 : 10);
			WMMapSubwidgets(row);
		}
		WPrefsPackGroup(delays, &group, True, k + 1 == wlengthof(clip_delay_frame_titles) ? 0 : 12);
	}

	WMPackBoxSubview(columns, delays, WMGetBoxMinimumSize(delays), True, 12);
	dock = WPrefsCreateGroup(columns, _("Dock/Clip/Drawer"));

	for (i = 0; i < wlengthof(dock_config); i++)
	{
		WMBox *row = WMCreateBox(dock.content);
		WMBox *before = WMCreateBox(row);
		WMBox *after = WMCreateBox(row);

		WMSetBoxHorizontal(row, True);
		panel->docksB[i] = WMCreateButton(row, WBTToggle);
		WMSetButtonImagePosition(panel->docksB[i], WIPImageOnly);
		CreateImages(scr, rc, xis, WMFontHeight(font) > 17 ? dock_config[i].preview_file :
			     dock_config[i].icon_file, &icon1, &icon2);
		WPrefsSetPreviewButtonFont(panel->docksB[i], font, icon2, icon1);
		if (icon2)
			WMReleasePixmap(icon2);
		if (icon1)
			WMReleasePixmap(icon1);
		WMSetBalloonTextForView(_(dock_config[i].balloon_text), WMWidgetView(panel->docksB[i]));
		WMSetButtonAction(panel->docksB[i], pushDockButton, panel);
		WMPackBoxSubview(row, before, wmksize(1, 1), True, 0);
		WPrefsPackButton(row, panel->docksB[i], 0);
		WMPackBoxSubview(row, after, wmksize(1, 1), True, 0);
		WMPackBoxSubview(dock.content, row, WMGetBoxMinimumSize(row), False,
				 i + 1 == wlengthof(dock_config) ? 0 : 8);
		WMMapSubwidgets(row);
	}

	WPrefsPackGroup(columns, &dock, False, 0);
	WMPackBoxSubview(panel->box, columns, WMGetBoxMinimumSize(columns), True, 0);
	WMMapSubwidgets(delays);
	WMMapSubwidgets(columns);
	WMReleaseFont(font);
	WMReleaseColor(color);

	if (xis)
		RReleaseImage(xis);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

static void storeData(_Panel *panel)
{
	int i;
	for (i = 0; i < wlengthof(auto_delay); i++)
	{
		char *str;

		str = WMGetTextFieldText(panel->autoDelayT[i]);
		SetStringForKey(str, auto_delay[i].key);
		wfree(str);
	}
	for (i = 0; i < wlengthof(dock_config); i++)
	{
		SetBoolForKey(!WMGetButtonSelected(panel->docksB[i]), dock_config[i].disable_key);
	}
}

static void showData(_Panel *panel)
{
	char *value;
	int i;
	for (i = 0; i < wlengthof(auto_delay); i++)
	{
		value = GetStringForKey(auto_delay[i].key);
		WMSetTextFieldText(panel->autoDelayT[i], value);
		adjustButtonSelectionBasedOnValue(panel, i, value);
	}
	for (i = 0; i < wlengthof(dock_config); i++)
	{
		WMSetButtonSelected(panel->docksB[i], !GetBoolForKey(dock_config[i].disable_key));
	}
}

Panel *InitDocks(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));
	memset(panel, 0, sizeof(_Panel));

	panel->sectionName = _("Dock Preferences");

	panel->description = _("Dock and clip features.\n"
			       "Enable/disable the Dock and Clip, and tune some delays.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
