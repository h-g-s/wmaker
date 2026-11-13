/* Focused model, persistence, transaction and WINGs interaction tests. */
#include "WPrefs.h"
#include "MonitorLayout.h"
#undef NDEBUG
#include <assert.h>
#include <unistd.h>

#ifdef USE_RANDR
static int fail_configuration;
static Status testSetCrtcConfig(Display *, XRRScreenResources *, RRCrtc, Time,
				int, int, RRMode, Rotation, RROutput *, int);
static XRRCrtcInfo *testGetCrtcInfo(Display *, XRRScreenResources *, RRCrtc);
static void testFreeCrtcInfo(XRRCrtcInfo *);
static XRRScreenResources allocation_resources;
static XRRCrtcInfo allocation_info;
#define XRRSetCrtcConfig testSetCrtcConfig
#define XRRGetCrtcInfo testGetCrtcInfo
#define XRRFreeCrtcInfo testFreeCrtcInfo
#include "MonitorLayout.c"
#undef XRRSetCrtcConfig
#undef XRRGetCrtcInfo
#undef XRRFreeCrtcInfo
#define main monitorEditorMain
#include "wmmonitors.c"
#undef main

static Status testSetCrtcConfig(Display *dpy, XRRScreenResources *resources, RRCrtc crtc,
				Time time, int x, int y, RRMode mode, Rotation rotation,
				RROutput *outputs, int count)
{
	if (mode != None && fail_configuration) {
		fail_configuration--;
		return RRSetConfigFailed;
	}
	return XRRSetCrtcConfig(dpy, resources, crtc, time, x, y, mode, rotation, outputs, count);
}

static XRRCrtcInfo *testGetCrtcInfo(Display *dpy, XRRScreenResources *resources, RRCrtc crtc)
{
	if (resources == &allocation_resources)
		return &allocation_info;
	return XRRGetCrtcInfo(dpy, resources, crtc);
}

static void testFreeCrtcInfo(XRRCrtcInfo *info)
{
	if (info != &allocation_info)
		XRRFreeCrtcInfo(info);
}

static MonitorLayout *fixture(void)
{
	MonitorLayout *layout = wmalloc(sizeof(*layout));
	int i;

	layout->count = 3;
	layout->primary = 1;
	layout->width = 3840;
	layout->height = 1080;
	layout->min_width = layout->min_height = 1;
	layout->max_width = layout->max_height = 8192;
	layout->outputs = wmalloc(sizeof(*layout->outputs) * layout->count);
	for (i = 0; i < layout->count; i++) {
		MonitorOutput *o = &layout->outputs[i];
		char name[20];

		snprintf(name, sizeof(name), "DP-%d", i + 1);
		o->id = i + 1;
		o->name = wstrdup(name);
		o->connected = True;
		o->enabled = i < 2;
		o->rotation = RR_Rotate_0;
		o->supported = 63;
		o->x = i * 1920;
		o->nmode = 1;
		o->modes = wmalloc(sizeof(*o->modes));
		o->modes[0].id = 10;
		o->modes[0].name = wstrdup("1920x1080");
		o->modes[0].width = 1920;
		o->modes[0].height = 1080;
		o->modes[0].rate = 60000;
		o->crtcs = wmalloc(sizeof(RRCrtc));
	}
	return layout;
}

static void modelTests(const char *path)
{
	MonitorLayout *layout = fixture(), *copy;
	char error[512];
	int w, h;

	assert(MonitorLayoutValidate(layout, error, sizeof(error)));
	layout->outputs[0].x = -1920;
	layout->outputs[1].x = 0;
	layout->outputs[0].y = layout->outputs[1].y = -100;
	MonitorLayoutNormalize(layout);
	assert(layout->outputs[0].x == 0 && layout->outputs[1].x == 1920);
	assert(layout->outputs[0].y == 0 && layout->outputs[1].y == 0);
	layout->outputs[0].rotation = RR_Rotate_90 | RR_Reflect_X;
	MonitorOutputSize(&layout->outputs[0], &w, &h);
	assert(w == 1080 && h == 1920);
	layout->outputs[1].x = 1080;
	assert(MonitorLayoutValidate(layout, error, sizeof(error)));
	assert(MonitorLayoutSave(layout, path, error, sizeof(error)));
	copy = MonitorLayoutCopy(layout);
	copy->outputs[0].id = 100;
	copy->outputs[0].rotation = RR_Rotate_0;
	copy->outputs[1].x = 500;
	copy->primary = 2;
	assert(MonitorLayoutLoad(copy, path, error, sizeof(error)));
	assert(copy->primary == 100);
	assert(copy->outputs[0].rotation == (RR_Rotate_90 | RR_Reflect_X));
	assert(copy->outputs[1].x == 1080 && !copy->outputs[2].enabled);
	copy->outputs[0].modes[0].rate = 59940;
	assert(!MonitorLayoutLoad(copy, path, error, sizeof(error)));
	assert(copy->outputs[0].modes[0].rate == 59940);
	MonitorLayoutFree(copy);
	layout->primary = 3;
	assert(!MonitorLayoutValidate(layout, error, sizeof(error)));
	layout->primary = 1;
	layout->outputs[0].supported = RR_Rotate_0;
	assert(!MonitorLayoutValidate(layout, error, sizeof(error)));
	layout->outputs[0].supported = 63;
	layout->outputs[1].x = 8192;
	assert(!MonitorLayoutValidate(layout, error, sizeof(error)));
	layout->outputs[0].enabled = layout->outputs[1].enabled = False;
	assert(!MonitorLayoutValidate(layout, error, sizeof(error)));
	MonitorLayoutFree(layout);
	{
		WMPropList *invalid = WMCreatePropListFromDescription(
			"{Version=1; Outputs={\"DP-1\"={Enabled=YES; X=0; Y=0; Rotation=1;};};}");

		assert(invalid);
		assert(WMWritePropListToFile(invalid, path));
		WMReleasePropList(invalid);
		layout = fixture();
		copy = MonitorLayoutCopy(layout);
		assert(!MonitorLayoutLoad(copy, path, error, sizeof(error)));
		assert(MonitorLayoutSame(layout, copy, False));
		MonitorLayoutFree(copy);
		MonitorLayoutFree(layout);
	}
}

static void allocationTests(void)
{
	MonitorLayout *layout = fixture();
	RRCrtc crtcs[] = { 11, 12 };
	CrtcGroup groups[2] = {
		{ .output = &layout->outputs[0], .members = &layout->outputs[0].id, .count = 1 },
		{ .output = &layout->outputs[1], .members = &layout->outputs[1].id, .count = 1 }
	};

	allocation_resources.crtcs = crtcs;
	allocation_resources.ncrtc = 2;
	allocation_info.rotations = 63;
	layout->outputs[0].crtcs = wrealloc(layout->outputs[0].crtcs, sizeof(crtcs));
	memcpy(layout->outputs[0].crtcs, crtcs, sizeof(crtcs));
	layout->outputs[0].ncrtc = 2;
	layout->outputs[1].crtcs[0] = 11;
	layout->outputs[1].ncrtc = 1;
	assert(assignCrtcs(NULL, &allocation_resources, layout, groups, 2, 0));
	assert(groups[0].crtc == 12 && groups[1].crtc == 11);
	allocation_resources.ncrtc = 1;
	assert(!assignCrtcs(NULL, &allocation_resources, layout, groups, 2, 0));
	allocation_resources.ncrtc = 2;
	allocation_info.rotations = RR_Rotate_0;
	layout->outputs[0].rotation = RR_Rotate_90;
	assert(!assignCrtcs(NULL, &allocation_resources, layout, groups, 2, 0));
	MonitorLayoutFree(layout);
}

static void checkBounds(WMView *view)
{
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		if (child->self && WMWidgetClass(child->self) == WC_Button) {
			WMSize size = WMGetButtonPreferredSize(child->self);

			assert(size.width <= child->size.width && size.height <= child->size.height);
		}
		checkBounds(child);
	}
}

static void editorTests(Display *dpy, const char *path)
{
	MonitorEditor editor = { 0 };
	WMRect rect;
	XEvent event = { 0 };
	MonitorOutput *o;
	Pixmap background;
	GC gc;
	XColor color, exact;
	Atom backgroundAtom = XInternAtom(dpy, "_XROOTPMAP_ID", False);
	Atom previousType;
	int previousFormat;
	unsigned long previousCount, after;
	unsigned char *previous = NULL;
	int i;

	editor.dpy = dpy;
	editor.root = DefaultRootWindow(dpy);
	editor.screen = WMCreateScreen(dpy, DefaultScreen(dpy));
	assert(editor.screen);
	editor.path = (char *)path;
	editor.selected = editor.dragging = -1;
	editor.draft = fixture();
	assert(XGetWindowProperty(dpy, editor.root, backgroundAtom, 0, 1, False, AnyPropertyType,
				  &previousType, &previousFormat, &previousCount, &after, &previous) == Success);
	background = XCreatePixmap(dpy, editor.root, 3840, 1080, DefaultDepth(dpy, DefaultScreen(dpy)));
	gc = XCreateGC(dpy, background, 0, NULL);
	assert(XAllocNamedColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), "red", &color, &exact));
	XSetForeground(dpy, gc, color.pixel);
	XFillRectangle(dpy, background, gc, 0, 0, 1920, 1080);
	assert(XAllocNamedColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), "blue", &color, &exact));
	XSetForeground(dpy, gc, color.pixel);
	XFillRectangle(dpy, background, gc, 1920, 0, 1920, 1080);
	XChangeProperty(dpy, editor.root, backgroundAtom, XA_PIXMAP, 32, PropModeReplace,
			(unsigned char *)&background, 1);
	createEditor(&editor);
	assert(editor.backgrounds[0] && editor.backgrounds[0]->data[0] > 240 &&
	       editor.backgrounds[0]->data[2] < 10);
	assert(editor.backgrounds[1] && editor.backgrounds[1]->data[2] > 240 &&
	       editor.backgrounds[1]->data[0] < 10);
	assert(editor.backgrounds[2] && editor.backgrounds[2]->data[0] == 0x50 &&
	       editor.backgrounds[2]->data[2] == 0x75);
	checkBounds(WMWidgetView(editor.window));
	/* Button choices update only the draft, including a newly enabled monitor. */
	WMPerformButtonClick(editor.primary[1]);
	assert(editor.draft->primary == 2 && editor.live->primary == 1);
	WMPerformButtonClick(editor.enabled[2]);
	assert(editor.draft->outputs[2].enabled && editor.draft->outputs[2].x == 3840);
	/* Simulate dragging the third monitor below the first and edge snapping. */
	rect = editor.rects[2];
	event.xbutton.type = ButtonPress;
	event.xbutton.button = Button1;
	event.xbutton.x = rect.pos.x + 10;
	event.xbutton.y = rect.pos.y + 10;
	canvasEvent(&event, &editor);
	assert(editor.dragging == 2);
	event.xmotion.type = MotionNotify;
	event.xmotion.x = editor.dragX - (int)(3840 * editor.scale);
	event.xmotion.y = editor.dragY + (int)(1080 * editor.scale);
	canvasEvent(&event, &editor);
	event.xbutton.type = ButtonRelease;
	event.xbutton.button = Button1;
	canvasEvent(&event, &editor);
	assert(abs(editor.draft->outputs[2].x) <= 1);
	assert(abs(editor.draft->outputs[2].y - 1080) <= 1);
	editor.selected = 2;
	showOptions(&editor, 20, 20);
	WMSetPopUpButtonSelectedItem(editor.rotation, 1);
	WMSetButtonSelected(editor.flipX, True);
	optionChanged(NULL, &editor);
	assert(editor.draft->outputs[2].rotation == (RR_Rotate_90 | RR_Reflect_X));
	/* Hotplug rebuilds safely and grows the controls to a second row. */
	freeBackgrounds(&editor);
	editor.live->outputs = wrealloc(editor.live->outputs, sizeof(*o) * 4);
	editor.draft->outputs = wrealloc(editor.draft->outputs, sizeof(*o) * 4);
	for (i = 0; i < 2; i++) {
		MonitorLayout *layout = i ? editor.draft : editor.live;

		o = &layout->outputs[3];
		memset(o, 0, sizeof(*o));
		o->name = wstrdup("HDMI-1");
		o->id = 4;
		o->connected = True;
		o->mode = -1;
		o->crtcs = wmalloc(sizeof(RRCrtc));
		o->modes = wmalloc(sizeof(MonitorMode));
		layout->count = 4;
	}
	grabBackgrounds(&editor);
	rebuild(&editor);
	checkBounds(WMWidgetView(editor.window));
	assert(editor.enabled[3] && !WMGetButtonEnabled(editor.enabled[3]));
	WMUnmapWidget(editor.options);
	freeBackgrounds(&editor);
	MonitorLayoutFree(editor.live);
	MonitorLayoutFree(editor.draft);
	WMDestroyWidget(editor.options);
	WMDestroyWidget(editor.confirmation);
	WMDestroyWidget(editor.window);
	wfree(editor.primary);
	wfree(editor.enabled);
	wfree(editor.rects);
	if (previousType != None)
		XChangeProperty(dpy, editor.root, backgroundAtom, previousType, previousFormat, PropModeReplace,
				previous, previousCount);
	else
		XDeleteProperty(dpy, editor.root, backgroundAtom);
	if (previous)
		XFree(previous);
	XFreeGC(dpy, gc);
	XFreePixmap(dpy, background);
}

static void transactionTests(Display *dpy)
{
	Window root = DefaultRootWindow(dpy);
	char error[512];
	MonitorLayout *before = MonitorLayoutQuery(dpy, root, error, sizeof(error));
	MonitorLayout *draft, *after;

	assert(before);
	draft = MonitorLayoutCopy(before);
	draft->primary = draft->outputs[0].id;
	assert(MonitorLayoutApply(dpy, root, draft, error, sizeof(error)));
	after = MonitorLayoutQuery(dpy, root, error, sizeof(error));
	assert(after && after->primary == draft->primary);
	MonitorLayoutFree(after);
	assert(MonitorLayoutRestore(dpy, root, before, error, sizeof(error)));
	fail_configuration = 1;
	assert(!MonitorLayoutApply(dpy, root, draft, error, sizeof(error)));
	assert(!fail_configuration);
	after = MonitorLayoutQuery(dpy, root, error, sizeof(error));
	assert(after && MonitorLayoutSame(before, after, False));
	MonitorLayoutFree(after);
	draft->outputs[0].connected = !draft->outputs[0].connected;
	assert(!MonitorLayoutApply(dpy, root, draft, error, sizeof(error)));
	MonitorLayoutFree(draft);
	MonitorLayoutFree(before);
}

static void timeoutTests(Display *dpy, const char *path)
{
	MonitorEditor editor = { 0 };
	MonitorLayout *before, *after;
	char error[512];

	editor.dpy = dpy;
	editor.root = DefaultRootWindow(dpy);
	editor.screen = WMCreateScreen(dpy, DefaultScreen(dpy));
	editor.path = (char *)path;
	editor.selected = editor.dragging = -1;
	editor.draft = MonitorLayoutQuery(dpy, editor.root, error, sizeof(error));
	assert(editor.draft);
	before = MonitorLayoutCopy(editor.draft);
	createEditor(&editor);
	editor.draft->primary = editor.draft->outputs[0].id;
	edited(&editor);
	applyChanges(NULL, &editor);
	assert(editor.rollback && editor.timer && !WMGetButtonEnabled(editor.save));
	WMDeleteTimerHandler(editor.timer);
	editor.timer = NULL;
	editor.seconds = 1;
	confirmationTick(&editor);
	assert(!editor.rollback && !editor.timer);
	after = MonitorLayoutQuery(dpy, editor.root, error, sizeof(error));
	assert(after && MonitorLayoutSame(before, after, False));
	MonitorLayoutFree(before);
	MonitorLayoutFree(after);
	freeBackgrounds(&editor);
	MonitorLayoutFree(editor.live);
	MonitorLayoutFree(editor.draft);
	WMDestroyWidget(editor.options);
	WMDestroyWidget(editor.confirmation);
	WMDestroyWidget(editor.window);
	wfree(editor.primary);
	wfree(editor.enabled);
	wfree(editor.rects);
}

int main(int argc, char **argv)
{
	char directory[] = "/tmp/wmmonitors-test-XXXXXX";
	char *path;
	Display *dpy;
	int result = 0;

	assert(mkdtemp(directory));
	assert(setenv("HOME", directory, 1) == 0);
	assert(setenv("WMAKER_USER_ROOT", directory, 1) == 0);
	path = wstrconcat(directory, "/layout");
	WMInitializeApplication("monitor_layout_test", &argc, argv);
	modelTests(path);
	allocationTests();
	dpy = XOpenDisplay(NULL);
	if (dpy) {
		transactionTests(dpy);
		editorTests(dpy, path);
		timeoutTests(dpy, path);
		XCloseDisplay(dpy);
		puts("Monitor layout, persistence, rollback and WINGs interaction tests passed");
	} else {
		puts("Model and persistence tests passed; SKIP: display tests require an isolated X server");
		result = 77;
	}
	assert(unlink(path) == 0);
	wfree(path);
	WMReleaseApplication();
	assert(rmdir(directory) == 0);
	return result;
}
#else
int main(void)
{
	puts("SKIP: RandR support is disabled");
	return 77;
}
#endif
