/* WallpaperChooser.h - browse a folder of images and set the wallpaper
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

#ifndef WPREFS_WALLPAPER_CHOOSER_H
#define WPREFS_WALLPAPER_CHOOSER_H

#include <WINGs/WINGs.h>

typedef struct WallpaperChooser WallpaperChooser;

/* Called for every WindowMaker default written when a wallpaper is applied. */
typedef void WallpaperChooserApplyHook(const char *key, WMPropList *value, void *data);

WallpaperChooser *WallpaperChooserCreate(WMWidget *parent);
void WallpaperChooserDestroy(WallpaperChooser *chooser);

WMBox *WallpaperChooserGetBox(WallpaperChooser *chooser);
WMSize WallpaperChooserGetMinimumSize(WallpaperChooser *chooser);

void WallpaperChooserSetApplyHook(WallpaperChooser *chooser, WallpaperChooserApplyHook *hook, void *data);

/* Scan the default background folder unless a folder was already chosen. */
void WallpaperChooserStart(WallpaperChooser *chooser);
void WallpaperChooserSetFolder(WallpaperChooser *chooser, const char *folder);

/* The user's Window Maker backgrounds folder, or a system folder with images. */
char *WallpaperChooserDefaultFolder(void);

#endif
