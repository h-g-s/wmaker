/* winoverview.h - full screen overview of the windows in the current workspace
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
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


#ifndef WMWINOVERVIEW_H_
#define WMWINOVERVIEW_H_

#include "screen.h"

/*
 * Cover the head with the pointer with a darkened copy of the wallpaper
 * and snapshots of all windows of the current workspace. The user picks a
 * window with the mouse or the keyboard; it is then raised and focused.
 */
void StartWindowOverview(WScreen *scr);

/*
 * Alt+Tab replacement: show the overview while the modifiers of the key
 * binding are held, cycling like the switch panel does. Returns False if
 * the switch panel should be used instead.
 */
Bool wWindowOverviewCycle(WWindow *wwin, XEvent *event, Bool next, Bool class_only);

/* Prepare the overview background once the screen is managed */
void wWindowOverviewInit(WScreen *scr);

/* Must be called for property changes on the root window */
void wWindowOverviewPropertyNotify(XPropertyEvent *event);

#endif
