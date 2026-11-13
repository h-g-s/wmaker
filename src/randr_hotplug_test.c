#include "randr.c"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#ifdef USE_RANDR
Display *dpy;
struct WPreferences wPreferences;
struct wmaker_global_variables w_global;

static XWindowAttributes root_attrs;
static XRRScreenResources resources;
static XRROutputInfo output_info;
static XRRCrtcInfo crtc_info;
static RRCrtc crtc = 2;
static RROutput output = 2;
static RRMode mode = 10;
static int max_width = 8192;
static int configured, resized, refreshed;
static int configured_x;
static Rotation configured_rotation;

Status XGetWindowAttributes(Display *display, Window window, XWindowAttributes *attrs)
{
	*attrs = root_attrs;
	return True;
}

Status XRRGetScreenSizeRange(Display *display, Window window,
                            int *min_w, int *min_h, int *max_w, int *max_h)
{
	*min_w = *min_h = 1;
	*max_w = max_width;
	*max_h = 8192;
	return True;
}

void XRRSetScreenSize(Display *display, Window window, int width, int height, int mm_w, int mm_h)
{
	assert(width >= root_attrs.width && height >= root_attrs.height);
	root_attrs.width = width;
	root_attrs.height = height;
	resized++;
}

XRROutputInfo *XRRGetOutputInfo(Display *display, XRRScreenResources *sr, RROutput xid)
{
	return &output_info;
}

void XRRFreeOutputInfo(XRROutputInfo *info)
{
}

XRRCrtcInfo *XRRGetCrtcInfo(Display *display, XRRScreenResources *sr, RRCrtc xid)
{
	return &crtc_info;
}

void XRRFreeCrtcInfo(XRRCrtcInfo *info)
{
}

Status XRRSetCrtcConfig(Display *display, XRRScreenResources *sr, RRCrtc xid, Time timestamp,
                       int x, int y, RRMode chosen_mode, Rotation rotation,
                       RROutput *outputs, int noutputs)
{
	assert(timestamp == CurrentTime);
	assert(chosen_mode == mode && noutputs == 1);
	assert(root_attrs.width >= x + (rotation == RR_Rotate_90 ? 1080 : 1920));
	configured_x = x;
	configured_rotation = rotation;
	configured++;
	return RRSetConfigSuccess;
}

RROutput XRRGetOutputPrimary(Display *display, Window window)
{
	return None;
}

XRRScreenResources *XRRGetScreenResourcesCurrent(Display *display, Window window)
{
	return &resources;
}

void XRRFreeScreenResources(XRRScreenResources *sr)
{
}

void wDefaultRefreshWorkspaceBackground(WScreen *scr) { refreshed++; }
void wDockSwap(WDock *dock) {}
void wClipSnapToHead(WDock *clip) {}
void wScreenUpdateUsableArea(WScreen *scr) {}
void wNETWMUpdateWorkarea(WScreen *scr) {}
void wWindowSnapToHead(WWindow *window) {}
void wArrangeIcons(WScreen *scr, Bool all) {}
WMRect wGetRectForHead(WScreen *scr, int head) { return scr->xine_info.screens[head]; }

int main(void)
{
	WScreen screen = { 0 };
	WRandRState state = { 0 };
	XRRModeInfo mode_info = { 0 };
	Screen xscreen = { 0 };

	dpy = (Display *)wmalloc(sizeof(*(_XPrivDisplay)dpy));
	((_XPrivDisplay)dpy)->screens = &xscreen;
	xscreen.mwidth = 510;
	xscreen.mheight = 290;
	root_attrs.width = 1920;
	root_attrs.height = 1080;
	screen.scr_width = 1920;
	screen.scr_height = 1080;
	screen.usableArea = wmalloc(sizeof(WArea));
	screen.totalUsableArea = wmalloc(sizeof(WArea));
	mode_info.id = mode;
	mode_info.width = 1920;
	mode_info.height = 1080;
	resources.nmode = 1;
	resources.modes = &mode_info;
	output_info.ncrtc = output_info.nmode = 1;
	output_info.crtcs = &crtc;
	output_info.modes = &mode;
	crtc_info.npossible = 1;
	crtc_info.possible = &output;
	crtc_info.rotations = RR_Rotate_0;
	state.n_outputs = 3;
	state.outputs[0].connected = True;
	state.outputs[0].crtc = 1;
	state.outputs[0].w = 1920;
	state.outputs[0].h = 1080;
	state.outputs[1].connected = state.outputs[2].connected = True;
	state.outputs[1].activate_pending = state.outputs[2].activate_pending = True;
	state.outputs[1].xid = output;
	state.outputs[2].xid = 3;

	/* Only one output is configured per resource snapshot, without overlap. */
	assert(wRandR_AutoActivate(&screen, &state, &resources));
	assert(configured == 1 && resized == 1 && configured_x == 1920);
	assert(root_attrs.width == 3840);
	state.outputs[1].crtc = crtc;
	state.outputs[1].x = configured_x;
	state.outputs[1].w = 1920;
	state.outputs[1].h = 1080;
	crtc = 3;
	output = state.outputs[2].xid;
	assert(wRandR_AutoActivate(&screen, &state, &resources));
	assert(configured == 2 && configured_x == 3840);
	assert(root_attrs.width == 5760);
	state.outputs[2].activate_pending = True;

	/* Reject a layout that cannot fit before issuing an invalid X request. */
	max_width = 5759;
	assert(!wRandR_AutoActivate(&screen, &state, &resources));
	assert(configured == 2 && resized == 2);

	/* Rotated modes use their actual footprint and preserve a wider framebuffer. */
	max_width = 8192;
	crtc_info.rotations = RR_Rotate_90;
	assert(wRandR_AutoActivate(&screen, &state, &resources));
	assert(configured_rotation == RR_Rotate_90);
	assert(root_attrs.width == 5760 && root_attrs.height == 1920);

	/* Both framebuffer growth and same-size rearrangements invalidate wallpaper. */
	assert(wRandR_ApplyToXinerama(&screen, &state));
	assert(screen.scr_width == 5760 && screen.scr_height == 1920);
	assert(!wRandR_ApplyToXinerama(&screen, &state));
	state.outputs[1].y = 10;
	assert(wRandR_ApplyToXinerama(&screen, &state));

	/* Geometry from external tools refreshes wallpaper even with activation off. */
	screen.randr_state = &state;
	state.n_outputs = 0;
	wPreferences.hotplug_monitor = False;
	wPreferences.flags.noclip = True;
	wRandR_Update(&screen);
	assert(refreshed == 1);
	wRandR_Update(&screen);
	assert(refreshed == 1);

	/* Successful activation schedules a fresh scan even without another event. */
	state.n_outputs = 1;
	memset(&state.outputs[0], 0, sizeof(state.outputs[0]));
	state.outputs[0].xid = output;
	resources.noutput = 1;
	resources.outputs = &output;
	output_info.connection = RR_Connected;
	wPreferences.hotplug_monitor = True;
	wRandR_Update(&screen);
	assert(screen.randr_debounce_timer);
	WMDeleteTimerHandler(screen.randr_debounce_timer);
	screen.randr_debounce_timer = NULL;
	output_info.crtc = crtc;
	crtc_info.width = 1080;
	crtc_info.height = 1920;
	wRandR_Update(&screen);
	assert(screen.xine_info.screens[0].size.width == 1080);
	assert(refreshed == 2);

	/* An external disable stays off; reconnecting still auto-activates it. */
	output_info.crtc = None;
	configured = 0;
	wRandR_Update(&screen);
	assert(configured == 0 && !state.outputs[0].activate_pending);
	output_info.connection = RR_Disconnected;
	wRandR_Update(&screen);
	output_info.connection = RR_Connected;
	wRandR_Update(&screen);
	assert(configured == 1);
	WMDeleteTimerHandler(screen.randr_debounce_timer);
	screen.randr_debounce_timer = NULL;

	wfree(screen.xine_info.screens);
	wfree(screen.usableArea);
	wfree(screen.totalUsableArea);
	wfree(dpy);
	puts("RandR activation, rotation, size limit and wallpaper invalidation tests passed");
	return 0;
}
#else
int main(void)
{
	puts("SKIP: RandR support is disabled");
	return 77;
}
#endif
