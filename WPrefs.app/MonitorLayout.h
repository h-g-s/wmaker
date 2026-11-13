/* RandR layout model shared by the monitor editor and its tests.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef MONITOR_LAYOUT_H
#define MONITOR_LAYOUT_H

#include "../src/wconfig.h"
#include <WINGs/WINGs.h>

#ifdef USE_RANDR
#include <X11/extensions/Xrandr.h>

typedef struct {
	RRMode id;
	char *name;
	int width, height;
	unsigned long rate;
} MonitorMode;

typedef struct {
	RROutput id;
	char *name;
	Bool connected, enabled;
	RRCrtc crtc;
	RRCrtc *crtcs;
	int ncrtc;
	MonitorMode *modes;
	int nmode, mode;
	int x, y;
	Rotation rotation, supported;
} MonitorOutput;

typedef struct {
	MonitorOutput *outputs;
	int count;
	RROutput primary;
	int width, height, mm_width, mm_height;
	int min_width, min_height, max_width, max_height;
	Bool transformed;
} MonitorLayout;

MonitorLayout *MonitorLayoutQuery(Display *dpy, Window root, char *error, size_t size);
MonitorLayout *MonitorLayoutCopy(const MonitorLayout *layout);
void MonitorLayoutFree(MonitorLayout *layout);
void MonitorOutputSize(const MonitorOutput *output, int *width, int *height);
void MonitorLayoutNormalize(MonitorLayout *layout);
Bool MonitorLayoutValidate(const MonitorLayout *layout, char *error, size_t size);
Bool MonitorLayoutSame(const MonitorLayout *a, const MonitorLayout *b, Bool topology_only);
Bool MonitorLayoutApply(Display *dpy, Window root, const MonitorLayout *layout, char *error, size_t size);
Bool MonitorLayoutRestore(Display *dpy, Window root, const MonitorLayout *layout, char *error, size_t size);
Bool MonitorLayoutSave(const MonitorLayout *layout, const char *path, char *error, size_t size);
Bool MonitorLayoutLoad(MonitorLayout *layout, const char *path, char *error, size_t size);
#endif
#endif
