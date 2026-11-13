/* MonitorLayout.c - live RandR configuration and explicit layout storage
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "MonitorLayout.h"

#ifdef USE_RANDR
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static Bool fail(char *error, size_t size, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsnprintf(error, size, format, args);
	va_end(args);
	return False;
}

void MonitorLayoutFree(MonitorLayout *layout)
{
	int i, j;

	if (!layout)
		return;
	for (i = 0; i < layout->count; i++) {
		MonitorOutput *o = &layout->outputs[i];

		wfree(o->name);
		wfree(o->crtcs);
		for (j = 0; j < o->nmode; j++)
			wfree(o->modes[j].name);
		wfree(o->modes);
	}
	wfree(layout->outputs);
	wfree(layout);
}

MonitorLayout *MonitorLayoutCopy(const MonitorLayout *layout)
{
	MonitorLayout *copy = wmalloc(sizeof(*copy));
	int i, j;

	*copy = *layout;
	copy->outputs = wmalloc(sizeof(*copy->outputs) * WMAX(1, layout->count));
	for (i = 0; i < layout->count; i++) {
		MonitorOutput *o = &copy->outputs[i];
		const MonitorOutput *src = &layout->outputs[i];

		*o = *src;
		o->name = wstrdup(src->name);
		o->crtcs = wmalloc(sizeof(*o->crtcs) * WMAX(1, o->ncrtc));
		memcpy(o->crtcs, src->crtcs, sizeof(*o->crtcs) * o->ncrtc);
		o->modes = wmalloc(sizeof(*o->modes) * WMAX(1, o->nmode));
		for (j = 0; j < o->nmode; j++) {
			o->modes[j] = src->modes[j];
			o->modes[j].name = wstrdup(src->modes[j].name);
		}
	}
	return copy;
}

static unsigned long modeRate(const XRRModeInfo *mode)
{
	double rate;

	if (!mode->hTotal || !mode->vTotal)
		return 0;
	rate = (double)mode->dotClock * 1000 / mode->hTotal / mode->vTotal;
	if (mode->modeFlags & RR_DoubleScan)
		rate /= 2;
	if (mode->modeFlags & RR_Interlace)
		rate *= 2;
	return (unsigned long)(rate + 0.5);
}

MonitorLayout *MonitorLayoutQuery(Display *dpy, Window root, char *error, size_t size)
{
	XRRScreenResources *resources;
	XWindowAttributes attrs;
	MonitorLayout *layout;
	int i, j, k;

	if (!XGetWindowAttributes(dpy, root, &attrs)) {
		fail(error, size, _("Could not query the desktop geometry."));
		return NULL;
	}
	resources = XRRGetScreenResourcesCurrent(dpy, root);
	if (!resources) {
		fail(error, size, _("Could not query RandR outputs."));
		return NULL;
	}
	layout = wmalloc(sizeof(*layout));
	layout->width = attrs.width;
	layout->height = attrs.height;
	layout->mm_width = WidthMMOfScreen(attrs.screen);
	layout->mm_height = HeightMMOfScreen(attrs.screen);
	layout->primary = XRRGetOutputPrimary(dpy, root);
	layout->outputs = wmalloc(sizeof(*layout->outputs) * WMAX(1, resources->noutput));
	if (!XRRGetScreenSizeRange(dpy, root, &layout->min_width, &layout->min_height,
				  &layout->max_width, &layout->max_height))
		goto query_failed;
	for (i = 0; i < resources->noutput; i++) {
		XRROutputInfo *info = XRRGetOutputInfo(dpy, resources, resources->outputs[i]);
		MonitorOutput *o = &layout->outputs[layout->count];

		if (!info)
			goto query_failed;
		layout->count++;
		o->id = resources->outputs[i];
		o->name = wmalloc(info->nameLen + 1);
		memcpy(o->name, info->name, info->nameLen);
		o->name[info->nameLen] = '\0';
		o->connected = info->connection == RR_Connected;
		o->crtc = info->crtc;
		o->rotation = RR_Rotate_0;
		o->mode = -1;
		o->ncrtc = info->ncrtc;
		o->crtcs = wmalloc(sizeof(*o->crtcs) * WMAX(1, o->ncrtc));
		memcpy(o->crtcs, info->crtcs, sizeof(*o->crtcs) * o->ncrtc);
		o->modes = wmalloc(sizeof(*o->modes) * WMAX(1, info->nmode));
		for (j = 0; j < info->nmode; j++) {
			for (k = 0; k < resources->nmode; k++) {
				XRRModeInfo *m = &resources->modes[k];
				MonitorMode *mode;

				if (m->id != info->modes[j])
					continue;
				mode = &o->modes[o->nmode++];
				mode->id = m->id;
				mode->width = m->width;
				mode->height = m->height;
				mode->rate = modeRate(m);
				mode->name = wmalloc(m->nameLength + 1);
				memcpy(mode->name, m->name, m->nameLength);
				mode->name[m->nameLength] = '\0';
				break;
			}
		}
		XRRFreeOutputInfo(info);
		for (j = 0; j < o->ncrtc; j++) {
			XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, resources, o->crtcs[j]);

			if (!ci)
				goto query_failed;
			o->supported |= ci->rotations;
			if (o->crtcs[j] == o->crtc) {
				o->enabled = ci->mode != None;
				o->x = ci->x;
				o->y = ci->y;
				o->rotation = ci->rotation;
				for (k = 0; k < o->nmode; k++)
					if (o->modes[k].id == ci->mode)
						o->mode = k;
			}
			XRRFreeCrtcInfo(ci);
		}
		if (o->mode < 0 && o->nmode)
			o->mode = 0;
	}
	for (i = 0; i < resources->ncrtc; i++) {
		XRRCrtcTransformAttributes *transform = NULL;
		XRRPanning *panning = XRRGetPanning(dpy, resources, resources->crtcs[i]);

		if (panning) {
			if (panning->width || panning->height)
				layout->transformed = True;
			XRRFreePanning(panning);
		}
		if (XRRGetCrtcTransform(dpy, resources->crtcs[i], &transform) && transform) {
			for (j = 0; j < 3; j++)
				for (k = 0; k < 3; k++)
					if (transform->currentTransform.matrix[j][k] != (j == k ? 65536 : 0))
						layout->transformed = True;
			XFree(transform);
		}
	}
	XRRFreeScreenResources(resources);
	return layout;

query_failed:
	XRRFreeScreenResources(resources);
	MonitorLayoutFree(layout);
	fail(error, size, _("The monitor configuration changed while it was being read. Try again."));
	return NULL;
}

void MonitorOutputSize(const MonitorOutput *o, int *width, int *height)
{
	*width = *height = 0;
	if (o->mode < 0 || o->mode >= o->nmode)
		return;
	*width = o->modes[o->mode].width;
	*height = o->modes[o->mode].height;
	if (o->rotation & (RR_Rotate_90 | RR_Rotate_270)) {
		int tmp = *width;

		*width = *height;
		*height = tmp;
	}
}

void MonitorLayoutNormalize(MonitorLayout *layout)
{
	int i, min_x = INT_MAX, min_y = INT_MAX;

	for (i = 0; i < layout->count; i++)
		if (layout->outputs[i].enabled) {
			min_x = WMIN(min_x, layout->outputs[i].x);
			min_y = WMIN(min_y, layout->outputs[i].y);
		}
	if (min_x == INT_MAX)
		return;
	for (i = 0; i < layout->count; i++)
		if (layout->outputs[i].enabled) {
			layout->outputs[i].x -= min_x;
			layout->outputs[i].y -= min_y;
		}
}

Bool MonitorLayoutValidate(const MonitorLayout *layout, char *error, size_t size)
{
	int i, enabled = 0;
	Bool primary = layout->primary == None;

	if (layout->transformed)
		return fail(error, size, _("Scaling or panning is active. Reset it with xrandr before editing this layout."));
	for (i = 0; i < layout->count; i++) {
		const MonitorOutput *o = &layout->outputs[i];
		int w, h;
		Rotation rotate = o->rotation & 15;

		if (!o->enabled)
			continue;
		enabled++;
		if (!o->connected || o->mode < 0 || o->mode >= o->nmode)
			return fail(error, size, _("Monitor %s has no connected, usable mode."), o->name);
		if (!rotate || (rotate & (rotate - 1)) || (o->rotation & ~o->supported))
			return fail(error, size, _("Monitor %s does not support this rotation or reflection."), o->name);
		MonitorOutputSize(o, &w, &h);
		if (w <= 0 || h <= 0 || o->x < 0 || o->y < 0 ||
		    o->x > layout->max_width - w || o->y > layout->max_height - h)
			return fail(error, size, _("The layout exceeds the maximum desktop size (%dx%d)."),
				    layout->max_width, layout->max_height);
		if (o->id == layout->primary)
			primary = True;
	}
	if (!enabled)
		return fail(error, size, _("At least one monitor must remain enabled."));
	if (!primary)
		return fail(error, size, _("The primary monitor must be enabled."));
	return True;
}

Bool MonitorLayoutSame(const MonitorLayout *a, const MonitorLayout *b, Bool topology_only)
{
	int i, j;

	if (a->count != b->count)
		return False;
	for (i = 0; i < a->count; i++) {
		const MonitorOutput *o = &a->outputs[i], *p = &b->outputs[i];

		if (o->id != p->id || strcmp(o->name, p->name) || o->connected != p->connected ||
		    o->nmode != p->nmode || o->ncrtc != p->ncrtc || o->supported != p->supported)
			return False;
		for (j = 0; j < o->nmode; j++)
			if (o->modes[j].id != p->modes[j].id)
				return False;
		for (j = 0; j < o->ncrtc; j++)
			if (o->crtcs[j] != p->crtcs[j])
				return False;
		if (!topology_only && (o->enabled != p->enabled ||
		    (o->enabled && (o->mode != p->mode || o->x != p->x || o->y != p->y ||
				   o->rotation != p->rotation))))
			return False;
	}
	return topology_only || (a->primary == b->primary && a->width == b->width &&
				a->height == b->height && a->transformed == b->transformed);
}

typedef struct {
	const MonitorOutput *output;
	RROutput *members;
	int count;
	RRCrtc crtc;
} CrtcGroup;

static Bool supportsCrtc(const MonitorLayout *layout, const CrtcGroup *group, RRCrtc crtc)
{
	int i, j, k;

	for (i = 0; i < group->count; i++) {
		for (j = 0; j < layout->count; j++)
			if (layout->outputs[j].id == group->members[i])
				break;
		if (j == layout->count)
			return False;
		for (k = 0; k < layout->outputs[j].ncrtc; k++)
			if (layout->outputs[j].crtcs[k] == crtc)
				break;
		if (k == layout->outputs[j].ncrtc)
			return False;
	}
	return True;
}

/* Backtrack because greedily taking the first CRTC can strand a constrained output. */
static Bool assignCrtcs(Display *dpy, XRRScreenResources *resources, const MonitorLayout *layout,
		       CrtcGroup *groups, int count, int index)
{
	int i, j;

	if (index == count)
		return True;
	if (count > resources->ncrtc)
		return False;
	for (i = -1; i < resources->ncrtc; i++) {
		RRCrtc crtc = i < 0 ? groups[index].output->crtc : resources->crtcs[i];
		XRRCrtcInfo *info;
		Bool supported;

		if (crtc == None || (i >= 0 && crtc == groups[index].output->crtc))
			continue;
		for (j = 0; j < index; j++)
			if (groups[j].crtc == crtc)
				break;
		if (j < index || !supportsCrtc(layout, &groups[index], crtc))
			continue;
		info = XRRGetCrtcInfo(dpy, resources, crtc);
		supported = info && !(groups[index].output->rotation & ~info->rotations);
		if (info)
			XRRFreeCrtcInfo(info);
		if (!supported)
			continue;
		groups[index].crtc = crtc;
		if (assignCrtcs(dpy, resources, layout, groups, count, index + 1))
			return True;
	}
	return False;
}

static int x_error;

static int configurationError(Display *dpy, XErrorEvent *event)
{
	(void)dpy;
	x_error = event->error_code;
	return 0;
}

static Bool configure(Display *dpy, Window root, const MonitorLayout *layout, Bool restore_size,
		      Bool *touched, char *error, size_t size)
{
	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(dpy, root);
	CrtcGroup *groups;
	int i, j, count = 0, width = layout->min_width, height = layout->min_height;
	Bool success = False;

	if (!resources)
		return fail(error, size, _("Could not query RandR resources."));
	groups = wmalloc(sizeof(*groups) * WMAX(1, layout->count));
	for (i = 0; i < layout->count; i++) {
		const MonitorOutput *o = &layout->outputs[i];
		int w, h;

		if (!o->enabled)
			continue;
		MonitorOutputSize(o, &w, &h);
		width = WMAX(width, o->x + w);
		height = WMAX(height, o->y + h);
		for (j = 0; j < count; j++) {
			const MonitorOutput *p = groups[j].output;

			if (o->crtc != None && o->crtc == p->crtc && o->x == p->x && o->y == p->y &&
			    o->modes[o->mode].id == p->modes[p->mode].id && o->rotation == p->rotation)
				break;
		}
		if (j == count) {
			groups[count].output = o;
			groups[count].members = wmalloc(sizeof(RROutput) * WMAX(1, layout->count));
			count++;
		}
		groups[j].members[groups[j].count++] = o->id;
	}
	if (!assignCrtcs(dpy, resources, layout, groups, count, 0)) {
		fail(error, size, _("No compatible display controllers are available for this layout."));
		goto done;
	}
	if (restore_size) {
		width = layout->width;
		height = layout->height;
	}
	*touched = True;
	for (i = 0; i < resources->ncrtc; i++)
		if (XRRSetCrtcConfig(dpy, resources, resources->crtcs[i], CurrentTime, 0, 0,
				     None, RR_Rotate_0, NULL, 0) != RRSetConfigSuccess) {
			fail(error, size, _("Could not release a display controller."));
			goto done;
		}
	XRRSetScreenSize(dpy, root, width, height,
			WMAX(1, (int)((long long)WMAX(1, layout->mm_width) * width / layout->width)),
			WMAX(1, (int)((long long)WMAX(1, layout->mm_height) * height / layout->height)));
	XSync(dpy, False);
	if (x_error) {
		fail(error, size, _("The X server rejected the desktop size (X error %d)."), x_error);
		goto done;
	}
	for (i = 0; i < count; i++) {
		const MonitorOutput *o = groups[i].output;

		if (XRRSetCrtcConfig(dpy, resources, groups[i].crtc, CurrentTime, o->x, o->y,
				     o->modes[o->mode].id, o->rotation, groups[i].members,
				     groups[i].count) != RRSetConfigSuccess) {
			fail(error, size, _("The X server rejected the configuration for %s."), o->name);
			goto done;
		}
	}
	XRRSetOutputPrimary(dpy, root, layout->primary);
	XSync(dpy, False);
	if (x_error)
		fail(error, size, _("The X server rejected the layout (X error %d)."), x_error);
	else
		success = True;
done:
	for (i = 0; i < count; i++)
		wfree(groups[i].members);
	wfree(groups);
	XRRFreeScreenResources(resources);
	return success;
}

static Bool applyLayout(Display *dpy, Window root, const MonitorLayout *layout, Bool restore_size,
			char *error, size_t size)
{
	MonitorLayout *before;
	int (*handler)(Display *, XErrorEvent *);
	Bool success, touched = False;
	char restore_error[512];

	if (!MonitorLayoutValidate(layout, error, size))
		return False;
	XGrabServer(dpy);
	before = MonitorLayoutQuery(dpy, root, error, size);
	if (!before) {
		XUngrabServer(dpy);
		return False;
	}
	if (!MonitorLayoutSame(before, layout, True)) {
		MonitorLayoutFree(before);
		XUngrabServer(dpy);
		return fail(error, size, _("Available monitors or modes changed. Refresh the layout before applying."));
	}
	if (before->transformed) {
		MonitorLayoutFree(before);
		XUngrabServer(dpy);
		return fail(error, size, _("Scaling or panning is active. Reset it with xrandr before editing this layout."));
	}
	XSync(dpy, False);
	x_error = 0;
	handler = XSetErrorHandler(configurationError);
	success = configure(dpy, root, layout, restore_size, &touched, error, size);
	if (!success && touched) {
		x_error = 0;
		if (!configure(dpy, root, before, True, &touched, restore_error, sizeof(restore_error))) {
			size_t used = strlen(error);

			wwarning(_("Monitor layout rollback failed: %s"), restore_error);
			snprintf(error + used, size > used ? size - used : 0, _("\nRollback failed: %s"), restore_error);
		}
	}
	XSync(dpy, False);
	XSetErrorHandler(handler);
	MonitorLayoutFree(before);
	XUngrabServer(dpy);
	XFlush(dpy);
	return success;
}

Bool MonitorLayoutApply(Display *dpy, Window root, const MonitorLayout *layout, char *error, size_t size)
{
	return applyLayout(dpy, root, layout, False, error, size);
}

Bool MonitorLayoutRestore(Display *dpy, Window root, const MonitorLayout *layout, char *error, size_t size)
{
	return applyLayout(dpy, root, layout, True, error, size);
}

static WMPropList *getValue(WMPropList *dict, const char *name)
{
	WMPropList *key = WMCreatePLString(name), *value = WMGetFromPLDictionary(dict, key);

	WMReleasePropList(key);
	return value;
}

static void setValue(WMPropList *dict, const char *name, const char *text)
{
	WMPropList *key = WMCreatePLString(name), *value = WMCreatePLString(text);

	WMPutInPLDictionary(dict, key, value);
	WMReleasePropList(key);
	WMReleasePropList(value);
}

static void setNumber(WMPropList *dict, const char *name, long value)
{
	char text[40];

	snprintf(text, sizeof(text), "%ld", value);
	setValue(dict, name, text);
}

Bool MonitorLayoutSave(const MonitorLayout *layout, const char *path, char *error, size_t size)
{
	WMPropList *dict = WMCreatePLDictionary(NULL, NULL);
	WMPropList *outputs = WMCreatePLDictionary(NULL, NULL), *key;
	Bool success;
	int i;

	if (!MonitorLayoutValidate(layout, error, size)) {
		WMReleasePropList(dict);
		WMReleasePropList(outputs);
		return False;
	}
	setValue(dict, "Version", "1");
	for (i = 0; i < layout->count; i++) {
		const MonitorOutput *o = &layout->outputs[i];
		WMPropList *entry;

		if (!o->connected)
			continue;
		entry = WMCreatePLDictionary(NULL, NULL);
		setNumber(entry, "Enabled", o->enabled);
		setNumber(entry, "X", o->x);
		setNumber(entry, "Y", o->y);
		setNumber(entry, "Rotation", o->rotation);
		if (o->mode >= 0) {
			setValue(entry, "Mode", o->modes[o->mode].name);
			setNumber(entry, "Rate", o->modes[o->mode].rate);
		}
		key = WMCreatePLString(o->name);
		WMPutInPLDictionary(outputs, key, entry);
		WMReleasePropList(key);
		WMReleasePropList(entry);
		if (o->id == layout->primary)
			setValue(dict, "Primary", o->name);
	}
	key = WMCreatePLString("Outputs");
	WMPutInPLDictionary(dict, key, outputs);
	WMReleasePropList(key);
	WMReleasePropList(outputs);
	success = WMWritePropListToFile(dict, path);
	WMReleasePropList(dict);
	if (!success)
		return fail(error, size, _("Could not save the monitor layout to %s."), path);
	return True;
}

static Bool readNumber(WMPropList *dict, const char *key, long *number)
{
	WMPropList *value = getValue(dict, key);
	char *text, *end;

	if (!value || !WMIsPLString(value))
		return False;
	text = WMGetFromPLString(value);
	errno = 0;
	*number = strtol(text, &end, 10);
	return !errno && end != text && !*end;
}

Bool MonitorLayoutLoad(MonitorLayout *layout, const char *path, char *error, size_t size)
{
	WMPropList *dict = WMReadPropListFromFile(path), *outputs, *primary;
	MonitorLayout *copy = MonitorLayoutCopy(layout);
	long version;
	int i, j, matched = 0;
	Bool success = False;

	if (!dict || !WMIsPLDictionary(dict) || !readNumber(dict, "Version", &version) || version != 1) {
		fail(error, size, _("Could not read a supported monitor layout from %s."), path);
		goto done;
	}
	outputs = getValue(dict, "Outputs");
	primary = getValue(dict, "Primary");
	if (!outputs || !WMIsPLDictionary(outputs) || (primary && !WMIsPLString(primary)))
		goto invalid;
	copy->primary = None;
	for (i = 0; i < copy->count; i++) {
		MonitorOutput *o = &copy->outputs[i];
		WMPropList *entry = getValue(outputs, o->name), *mode;
		long enabled, x, y, rotation, rate;

		if (!entry || !o->connected)
			continue;
		matched++;
		if (!WMIsPLDictionary(entry) || !readNumber(entry, "Enabled", &enabled) ||
		    !readNumber(entry, "X", &x) || !readNumber(entry, "Y", &y) ||
		    !readNumber(entry, "Rotation", &rotation) || (enabled != 0 && enabled != 1) ||
		    x < 0 || y < 0 || x > copy->max_width || y > copy->max_height ||
		    rotation < 1 || rotation > 63)
			goto invalid;
		o->enabled = enabled;
		o->x = x;
		o->y = y;
		o->rotation = rotation;
		mode = getValue(entry, "Mode");
		if (o->enabled) {
			if (!mode || !WMIsPLString(mode) || !readNumber(entry, "Rate", &rate) || rate < 0)
				goto invalid;
			for (j = 0; j < o->nmode; j++)
				if (!strcmp(o->modes[j].name, WMGetFromPLString(mode)) &&
				    o->modes[j].rate == (unsigned long)rate)
					break;
			if (j == o->nmode) {
				fail(error, size, _("The saved mode for %s is not available."), o->name);
				goto done;
			}
			o->mode = j;
		}
		if (primary && !strcmp(WMGetFromPLString(primary), o->name) && o->enabled)
			copy->primary = o->id;
	}
	if (!matched) {
		fail(error, size, _("No connected monitors match the saved layout."));
		goto done;
	}
	MonitorLayoutNormalize(copy);
	if (!MonitorLayoutValidate(copy, error, size))
		goto done;
	/* Missing outputs are ignored; newly attached outputs retain their live settings. */
	for (i = 0; i < layout->count; i++) {
		layout->outputs[i].enabled = copy->outputs[i].enabled;
		layout->outputs[i].x = copy->outputs[i].x;
		layout->outputs[i].y = copy->outputs[i].y;
		layout->outputs[i].mode = copy->outputs[i].mode;
		layout->outputs[i].rotation = copy->outputs[i].rotation;
	}
	layout->primary = copy->primary;
	success = True;
	goto done;
invalid:
	fail(error, size, _("The saved monitor layout contains invalid values."));
done:
	if (dict)
		WMReleasePropList(dict);
	MonitorLayoutFree(copy);
	return success;
}
#endif
