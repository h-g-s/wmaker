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
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef WMSNAPSHOT_H_
#define WMSNAPSHOT_H_

#include "window.h"

/* Largest size of the cached snapshot kept for each window */
#define SNAPSHOT_MAX_WIDTH	240
#define SNAPSHOT_MAX_HEIGHT	180

/* Start keeping window contents off-screen when Composite is available */
void wSnapshotInitScreen(WScreen *scr);

/* Draw img at x,y of dest. Alpha is ignored, combine it with a color first */
void wSnapshotDraw(WScreen *scr, Drawable dest, RImage *img, int x, int y);

/* True when window contents are kept off-screen by the Composite extension */
Bool wSnapshotUsesComposite(void);

/*
 * Draw the live content of the window scaled to w x h at x,y of dest, a
 * drawable of the screen depth. The scaling is done by the X server.
 * Only works with Composite; returns False when nothing was drawn.
 */
Bool wSnapshotRenderScaled(WWindow *wwin, Drawable dest, int x, int y, int w, int h);

/* True if a grab of the window would give its real content */
Bool wSnapshotCanGrab(WWindow *wwin, Window ignore);

/*
 * Grab the current contents of a mapped client window. With Composite the
 * whole window is read even when covered; otherwise it is clipped to the
 * screen. If raise is True the frame is raised first. Returns a full size
 * image the caller must release, or NULL.
 */
RImage *wSnapshotGrabWindow(WWindow *wwin, Bool raise);

/*
 * Return a copy of img scaled to fit inside max_w x max_h while keeping
 * its aspect ratio. Images are never enlarged. Caller releases the result.
 */
RImage *wSnapshotScaleToFit(RImage *img, int max_w, int max_h);

/* Store a reduced copy of a full size grab as the window's cached snapshot */
/*
 * Return a copy of img scaled to exactly w x h. Shrinking uses a fast box
 * filter. Caller releases the result.
 */
RImage *wSnapshotScale(RImage *img, int w, int h);

void wSnapshotStore(WWindow *wwin, RImage *full);

/* Grab and cache a snapshot of the window, if it is currently visible */
void wSnapshotUpdate(WWindow *wwin, Bool raise);

void wSnapshotRelease(WWindow *wwin);

/* True if no other mapped window covers the client area, so a grab of the
 * window shows only its own contents. The ignore window is not considered */
Bool wSnapshotIsUnobscured(WWindow *wwin, Window ignore);

/* Refresh the snapshot of a window shortly after it receives the focus */
void wSnapshotScheduleFocused(WWindow *wwin);

#endif
