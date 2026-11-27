/* monitor_layout.c - live RandR configuration and explicit layout storage
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "monitor_layout.h"

#ifdef USE_RANDR
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

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
		if (o->identity)
			wfree(o->identity);
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
		o->identity = src->identity ? wstrdup(src->identity) : NULL;
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

/* Connector names can change (e.g. DP-MST renumbering) or be reused by another
 * dock, so the EDID vendor, product and serial identify the attached monitor. */
static char *outputIdentity(Display *dpy, RROutput output, Atom edid)
{
	static const unsigned char header[] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
	unsigned char *data = NULL;
	unsigned long count, after;
	Atom type;
	int format, i;
	char *identity = NULL;

	if (edid == None ||
	    XRRGetOutputProperty(dpy, output, edid, 0, 32, False, False, AnyPropertyType,
				 &type, &format, &count, &after, &data) != Success)
		return NULL;
	if (data && format == 8 && count >= 18 && !memcmp(data, header, sizeof(header))) {
		identity = wmalloc(21);
		for (i = 0; i < 10; i++)
			snprintf(identity + i * 2, 3, "%02x", data[8 + i]);
	}
	if (data)
		XFree(data);
	return identity;
}

MonitorLayout *MonitorLayoutQuery(Display *dpy, Window root, char *error, size_t size)
{
	XRRScreenResources *resources;
	XWindowAttributes attrs;
	MonitorLayout *layout;
	Atom edid;
	int i, j, k;

	if (!XGetWindowAttributes(dpy, root, &attrs)) {
		fail(error, size, _("Could not query the desktop geometry."));
		return NULL;
	}
	edid = XInternAtom(dpy, RR_PROPERTY_RANDR_EDID, True);
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
		if (o->connected)
			o->identity = outputIdentity(dpy, o->id, edid);
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
		    (!o->identity) != (!p->identity) || (o->identity && strcmp(o->identity, p->identity)) ||
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

#define MAX_SAVED_LAYOUTS 32

static WMPropList *profileFromLayout(const MonitorLayout *layout)
{
	WMPropList *dict = WMCreatePLDictionary(NULL, NULL);
	WMPropList *outputs = WMCreatePLDictionary(NULL, NULL), *key;
	int i;

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
		if (o->identity)
			setValue(entry, "Identity", o->identity);
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
	return dict;
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

/* Returns a retained array of saved layouts, most recently saved first.
 * Version 1 files hold a single layout at the top level. */
static WMPropList *readProfiles(const char *path, Bool *missing, char *error, size_t size)
{
	WMPropList *dict, *profiles = NULL, *layouts;
	long version;
	int i;

	*missing = False;
	if (access(path, F_OK) < 0 && (errno == ENOENT || errno == ENOTDIR)) {
		*missing = True;
		fail(error, size, _("No monitor layout has been saved yet."));
		return NULL;
	}
	dict = WMReadPropListFromFile(path);
	if (!dict || !WMIsPLDictionary(dict) || !readNumber(dict, "Version", &version))
		goto unsupported;
	if (version == 1) {
		profiles = WMCreatePLArray(dict, NULL);
	} else if (version == 2) {
		layouts = getValue(dict, "Layouts");
		if (!layouts || !WMIsPLArray(layouts))
			goto unsupported;
		for (i = 0; i < WMGetPropListItemCount(layouts); i++)
			if (!WMIsPLDictionary(WMGetFromPLArray(layouts, i)))
				goto unsupported;
		profiles = WMRetainPropList(layouts);
	} else {
		goto unsupported;
	}
	WMReleasePropList(dict);
	return profiles;

unsupported:
	if (dict)
		WMReleasePropList(dict);
	fail(error, size, _("Could not read a supported monitor layout from %s."), path);
	return NULL;
}

static const char *entryIdentity(WMPropList *entry)
{
	WMPropList *value = entry && WMIsPLDictionary(entry) ? getValue(entry, "Identity") : NULL;

	return value && WMIsPLString(value) ? WMGetFromPLString(value) : NULL;
}

/* Map connected outputs to saved entries: by connector name when the monitor
 * identity agrees (or is unknown), then by identity alone for renamed
 * connectors.  Returns the number of mapped outputs, or -1 if invalid. */
static int matchProfile(WMPropList *profile, const MonitorLayout *layout, WMPropList **keys,
			Bool *exact)
{
	WMPropList *outputs = getValue(profile, "Outputs"), *names;
	int i, j, k, count, matched = 0, connected = 0;
	Bool *used;

	*exact = False;
	for (i = 0; i < layout->count; i++)
		keys[i] = NULL;
	if (!outputs || !WMIsPLDictionary(outputs))
		return -1;
	names = WMGetPLDictionaryKeys(outputs);
	count = WMGetPropListItemCount(names);
	for (j = 0; j < count; j++)
		if (!WMIsPLString(WMGetFromPLArray(names, j))) {
			WMReleasePropList(names);
			return -1;
		}
	used = wmalloc(sizeof(*used) * WMAX(1, count));
	for (i = 0; i < layout->count; i++) {
		const MonitorOutput *o = &layout->outputs[i];

		if (!o->connected)
			continue;
		connected++;
		for (j = 0; j < count; j++) {
			WMPropList *name = WMGetFromPLArray(names, j);
			const char *identity = entryIdentity(WMGetFromPLDictionary(outputs, name));

			if (!used[j] && !strcmp(WMGetFromPLString(name), o->name) &&
			    (!identity || !o->identity || !strcmp(identity, o->identity)))
				break;
		}
		if (j < count) {
			used[j] = True;
			keys[i] = WMGetFromPLArray(names, j);
			matched++;
		}
	}
	for (i = 0; i < layout->count; i++) {
		const MonitorOutput *o = &layout->outputs[i];
		int candidate = -1;

		if (!o->connected || keys[i] || !o->identity)
			continue;
		for (k = 0; k < layout->count; k++)
			if (k != i && layout->outputs[k].connected && !keys[k] && layout->outputs[k].identity &&
			    !strcmp(layout->outputs[k].identity, o->identity))
				break;
		if (k < layout->count)
			continue;
		for (j = 0; j < count; j++) {
			const char *identity = entryIdentity(WMGetFromPLDictionary(outputs, WMGetFromPLArray(names, j)));

			if (used[j] || !identity || strcmp(identity, o->identity))
				continue;
			candidate = candidate < 0 ? j : -2;
		}
		if (candidate >= 0) {
			used[candidate] = True;
			keys[i] = WMGetFromPLArray(names, candidate);
			matched++;
		}
	}
	*exact = matched == connected && matched == count;
	wfree(used);
	/* Keys stay valid while the profile dictionary retains them. */
	WMReleasePropList(names);
	return matched;
}

Bool MonitorLayoutSave(const MonitorLayout *layout, const char *path, char *error, size_t size)
{
	WMPropList *dict, *profiles, *saved, *key;
	WMPropList **keys;
	Bool success, missing, exact;
	int i;

	if (!MonitorLayoutValidate(layout, error, size))
		return False;
	keys = wmalloc(sizeof(*keys) * WMAX(1, layout->count));
	profiles = WMCreatePLArray(NULL);
	WMAddToPLArray(profiles, saved = profileFromLayout(layout));
	WMReleasePropList(saved);
	/* Keep layouts for other monitor sets; replace the one for this set. */
	saved = readProfiles(path, &missing, error, size);
	if (saved) {
		for (i = 0; i < WMGetPropListItemCount(saved); i++) {
			WMPropList *profile = WMGetFromPLArray(saved, i);

			if (WMGetPropListItemCount(profiles) >= MAX_SAVED_LAYOUTS)
				break;
			if (matchProfile(profile, layout, keys, &exact) < 0 || exact)
				continue;
			WMAddToPLArray(profiles, profile);
		}
		WMReleasePropList(saved);
	}
	wfree(keys);
	dict = WMCreatePLDictionary(NULL, NULL);
	setValue(dict, "Version", "2");
	key = WMCreatePLString("Layouts");
	WMPutInPLDictionary(dict, key, profiles);
	WMReleasePropList(key);
	WMReleasePropList(profiles);
	success = WMWritePropListToFile(dict, path);
	WMReleasePropList(dict);
	if (!success)
		return fail(error, size, _("Could not save the monitor layout to %s."), path);
	return True;
}

static Bool applyProfile(MonitorLayout *layout, WMPropList *profile, WMPropList **keys, Bool exact,
			 char *error, size_t size)
{
	WMPropList *outputs = getValue(profile, "Outputs"), *primary = getValue(profile, "Primary");
	MonitorLayout *copy;
	int i, j;
	Bool success = False;

	if (primary && !WMIsPLString(primary))
		return fail(error, size, _("The saved monitor layout contains invalid values."));
	copy = MonitorLayoutCopy(layout);
	copy->primary = None;
	for (i = 0; i < copy->count; i++) {
		MonitorOutput *o = &copy->outputs[i];
		WMPropList *entry, *mode;
		long enabled, x, y, rotation, rate;

		if (!o->connected) {
			/* Exact restores describe every connected monitor; release the rest. */
			if (exact)
				o->enabled = False;
			continue;
		}
		if (!keys[i])
			continue;
		entry = WMGetFromPLDictionary(outputs, keys[i]);
		if (!entry || !WMIsPLDictionary(entry) || !readNumber(entry, "Enabled", &enabled) ||
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
		if (primary && !strcmp(WMGetFromPLString(primary), WMGetFromPLString(keys[i])) && o->enabled)
			copy->primary = o->id;
	}
	MonitorLayoutNormalize(copy);
	if (!MonitorLayoutValidate(copy, error, size))
		goto done;
	/* Non-exact loads ignore missing outputs; newly attached outputs retain live settings. */
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
	MonitorLayoutFree(copy);
	return success;
}

static MonitorLayoutStatus monitorLayoutLoad(MonitorLayout *layout, const char *path, char *error,
					     size_t size, Bool exact)
{
	WMPropList *profiles, *best = NULL;
	WMPropList **keys = wmalloc(sizeof(*keys) * WMAX(1, layout->count));
	WMPropList **best_keys = wmalloc(sizeof(*keys) * WMAX(1, layout->count));
	MonitorLayoutStatus status = MONITOR_LAYOUT_FAILED;
	Bool missing, matches_all, best_exact = False;
	int i, matched, best_matched = 0;

	profiles = readProfiles(path, &missing, error, size);
	if (!profiles) {
		if (missing)
			status = MONITOR_LAYOUT_NONE;
		goto done;
	}
	for (i = 0; i < WMGetPropListItemCount(profiles) && !best_exact; i++) {
		WMPropList *profile = WMGetFromPLArray(profiles, i);

		matched = matchProfile(profile, layout, keys, &matches_all);
		if (matched < 0)
			continue;
		if (exact && !matches_all)
			continue;
		/* The first complete match wins; otherwise prefer the largest overlap. */
		if (!matches_all && matched <= best_matched)
			continue;
		best = profile;
		best_matched = matched;
		best_exact = matches_all;
		memcpy(best_keys, keys, sizeof(*keys) * layout->count);
	}
	if (!best) {
		status = MONITOR_LAYOUT_NONE;
		if (exact)
			fail(error, size, _("No saved layout matches the connected monitors."));
		else
			fail(error, size, _("No connected monitors match the saved layout."));
		goto done;
	}
	if (applyProfile(layout, best, best_keys, exact, error, size))
		status = MONITOR_LAYOUT_LOADED;
done:
	if (profiles)
		WMReleasePropList(profiles);
	wfree(keys);
	wfree(best_keys);
	return status;
}

Bool MonitorLayoutLoad(MonitorLayout *layout, const char *path, char *error, size_t size)
{
	return monitorLayoutLoad(layout, path, error, size, False) == MONITOR_LAYOUT_LOADED;
}

MonitorLayoutStatus MonitorLayoutLoadExact(MonitorLayout *layout, const char *path, char *error, size_t size)
{
	return monitorLayoutLoad(layout, path, error, size, True);
}
#endif
