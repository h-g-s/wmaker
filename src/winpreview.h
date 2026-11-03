/*
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

#ifndef WMWINPREVIEW_H_
#define WMWINPREVIEW_H_

#include "screen.h"
#include "window.h"

/*
 * Popup listing the windows of an application (when the pointer rests over
 * its application icon) or a single minimized window (over its miniwindow).
 * Each window is shown with its title and a reduced snapshot; clicking one
 * restores it if minimized/hidden, otherwise brings it to the front.
 */

/* Returns True if the event was taken over by the preview popup, in which
 * case the regular balloon must not be shown */
Bool wWinPreviewEnterNotify(WScreen *scr, WObjDescriptor *desc, XEvent *event);
void wWinPreviewLeaveNotify(XEvent *event);
Bool wWinPreviewMotionNotify(XEvent *event);

/* Hide the popup unless the click happened inside it */
void wWinPreviewButtonPress(XEvent *event);

void wWinPreviewHide(void);

/* Must be called before a window is freed */
void wWinPreviewForgetWindow(WWindow *wwin);

/* Restore a minimized/hidden window, or raise and focus an open one */
void wWinPreviewActivate(WWindow *wwin);

#endif
