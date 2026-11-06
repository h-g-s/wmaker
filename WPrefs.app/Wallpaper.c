/* Wallpaper.c - desktop wallpaper chooser panel
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
#include "WallpaperChooser.h"

typedef struct _Panel {
	WMBox *box;
	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WallpaperChooser *chooser;
	/* Defaults written by the chooser; reasserted on Save over stale panel data. */
	WMPropList *applied;
} _Panel;

#define ICON_FILE	"wallpaper"

static void wallpaperApplied(const char *key, WMPropList *value, void *data)
{
	_Panel *panel = data;
	WMPropList *name = WMCreatePLString(key);

	WMPutInPLDictionary(panel->applied, name, value);
	WMReleasePropList(name);
	SetObjectForKey(value, key);
}

static void storeData(_Panel *panel)
{
	WMPropList *keys = WMGetPLDictionaryKeys(panel->applied);
	int i;

	for (i = 0; i < WMGetPropListItemCount(keys); i++) {
		WMPropList *key = WMGetFromPLArray(keys, i);

		SetObjectForKey(WMGetFromPLDictionary(panel->applied, key), WMGetFromPLString(key));
	}
	WMReleasePropList(keys);
}

static void showPanel(_Panel *panel)
{
	WallpaperChooserStart(panel->chooser);
}

static void createPanel(Panel *p)
{
	_Panel *panel = (_Panel *)p;
	WMBox *box;

	panel->box = WPrefsCreatePanelBox(panel->parent);
	panel->chooser = WallpaperChooserCreate(panel->box);
	WallpaperChooserSetApplyHook(panel->chooser, wallpaperApplied, panel);
	box = WallpaperChooserGetBox(panel->chooser);
	WMPackBoxSubview(panel->box, box, WallpaperChooserGetMinimumSize(panel->chooser), True, 0);
	WMRealizeWidget(panel->box);
	WMMapSubwidgets(box);
	WMMapSubwidgets(panel->box);
}

Panel *InitWallpaper(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Wallpaper");

	panel->description = _("Choose the desktop background image, how it\n"
			       "is placed and on which monitor it is shown.");

	panel->parent = parent;
	panel->applied = WMCreatePLDictionary(NULL, NULL);

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.showPanel = showPanel;

	AddSection(panel, ICON_FILE);

	return panel;
}
