#define MonitorLayoutQuery testLayoutQuery
#define MonitorLayoutLoadExact testLayoutLoadExact
#define MonitorLayoutApply testLayoutApply
#define MonitorLayoutFree testLayoutFree
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
static MonitorLayoutStatus saved_status = MONITOR_LAYOUT_NONE;
static int loads, applied;
static MonitorLayout query_layout;

MonitorLayout *testLayoutQuery(Display *display, Window root, char *error, size_t size)
{
	return &query_layout;
}

MonitorLayoutStatus testLayoutLoadExact(MonitorLayout *layout, const char *path, char *error, size_t size)
{
	loads++;
	if (saved_status == MONITOR_LAYOUT_FAILED)
		snprintf(error, size, "mode not ready");
	return saved_status;
}

Bool testLayoutApply(Display *display, Window root, const MonitorLayout *layout, char *error, size_t size)
{
	applied++;
	return True;
}

void testLayoutFree(MonitorLayout *layout)
{
}

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
static WMRect dock_extent, clip_extent;
static int dock_head, maximized_head, maximized_calls;

void wDockMoveToEdge(WDock *dock, int head, Bool on_right_side, int y)
{
	WMRect r = dock->screen_ptr->xine_info.screens[head];

	dock_head = head;
	dock->on_right_side = on_right_side;
	dock->x_pos = on_right_side ? r.pos.x + (int)r.size.width - 64 : r.pos.x;
	dock->y_pos = y;
}

void wDockMove(WDock *dock, int x, int y)
{
	dock->x_pos = x;
	dock->y_pos = y;
}

WMRect wDockGetExtent(WDock *dock)
{
	return dock->type == WM_CLIP ? clip_extent : dock_extent;
}

void wWindowMove(WWindow *wwin, int x, int y)
{
	wwin->frame_x = x;
	wwin->frame_y = y;
}

void wWindowConfigure(WWindow *wwin, int x, int y, int width, int height)
{
	wwin->frame_x = x;
	wwin->frame_y = y;
	wwin->frame->core->width = width;
	wwin->frame->core->height = height;
}

void wWindowConstrainSize(WWindow *wwin, unsigned int *width, unsigned int *height) {}
void wWindowSynthConfigureNotify(WWindow *wwin) {}

void wMaximizeWindow(WWindow *wwin, int directions, int head)
{
	assert(directions & MAX_KEYBOARD);
	maximized_head = head;
	maximized_calls++;
}
void wScreenUpdateUsableArea(WScreen *scr) {}
void wNETWMUpdateWorkarea(WScreen *scr) {}
void wWindowSnapToHead(WWindow *window) {}
void wArrangeIcons(WScreen *scr, Bool all) {}
WMRect wGetRectForHead(WScreen *scr, int head) { return scr->xine_info.screens[head]; }

static void setHeads(WScreen *scr, WRandRState *state, int count, const char **names,
                     const int (*rects)[4])
{
	int i;

	wfree(scr->xine_info.screens);
	scr->xine_info.screens = wmalloc(sizeof(WMRect) * count);
	scr->xine_info.count = count;
	scr->xine_info.primary_head = 0;
	state->heads.count = count;
	state->heads.primary = 0;
	for (i = 0; i < count; i++) {
		WMRect r = { { rects[i][0], rects[i][1] }, { rects[i][2], rects[i][3] } };

		scr->xine_info.screens[i] = r;
		state->heads.rects[i] = r;
		strcpy(state->heads.names[i], names[i]);
	}
}

static void initWindow(WWindow *wwin, WFrameWindow *frame, WCoreWindow *core, WScreen *scr,
                       int x, int y, int w, int h)
{
	memset(wwin, 0, sizeof(*wwin));
	memset(frame, 0, sizeof(*frame));
	memset(core, 0, sizeof(*core));
	wwin->screen_ptr = scr;
	wwin->frame = frame;
	frame->core = core;
	wwin->frame_x = x;
	wwin->frame_y = y;
	core->width = w;
	core->height = h;
	wwin->prev = scr->focused_window;
	scr->focused_window = wwin;
}

/* Docking, undocking and redocking the three monitor setup */
static void placementTests(void)
{
	static const char *dock_names[] = { "eDP-1", "DP-1-3", "DP-1-2" };
	static const int dock_rects[][4] = {
		{ 4480, 0, 1920, 1200 }, { 0, 0, 1920, 1080 }, { 1920, 0, 2560, 1440 }
	};
	static const char *laptop_names[] = { "eDP-1" };
	static const int laptop_rects[][4] = { { 0, 0, 1920, 1200 } };
	WScreen scr = { 0 };
	WRandRState state = { 0 };
	WDock dock = { 0 }, clip = { 0 };
	WWorkspace workspace = { 0 }, *workspaces[1] = { &workspace };
	WWindow on_laptop, on_middle, huge, maximized;
	WFrameWindow frames[4];
	WCoreWindow cores[4];
	WRandRHeads old;
	char signature[256];

	/* Helpers */
	setHeads(&scr, &state, 3, dock_names, dock_rects);
	wRandR_HeadsSignature(&state.heads, signature, sizeof(signature));
	assert(strcmp(signature, "DP-1-2,DP-1-3,eDP-1") == 0);
	assert(wRandR_EdgeRelative(10, 1000, 500, 64) == 10);
	assert(wRandR_EdgeRelative(936, 1000, 500, 64) == 436);
	/* An oversized clip with icons to the right shows as many as possible */
	assert(wRandR_ClampSpan(1856, 0, 704, 64, 0, 1920) == 1216);
	assert(wRandR_ClampSpan(500, 0, 3000, 64, 0, 1920) == 0);
	assert(wRandR_ClampSpan(500, -3000, 64, 64, 0, 1920) == 1856);
	assert(wRandR_ClampSpan(-10, -128, 192, 64, 0, 1920) == 128);

	wPreferences.icon_size = 64;
	wPreferences.flags.noclip = False;
	scr.workspaces = workspaces;
	scr.workspace_count = 1;
	workspace.clip = &clip;
	scr.dock = &dock;
	dock.screen_ptr = clip.screen_ptr = &scr;
	dock.type = WM_DOCK;
	clip.type = WM_CLIP;
	clip_extent.size.width = 11 * 64;
	clip_extent.size.height = 64;
	dock_extent.size.width = 64;
	dock_extent.size.height = 5 * 64;

	/* Dock on the right edge of the laptop, clip at the bottom left of the
	 * middle monitor with its icons extending to the right */
	dock.on_right_side = True;
	dock.x_pos = 6400 - 64;
	dock.y_pos = 0;
	clip.x_pos = 1920;
	clip.y_pos = 1440 - 64;
	initWindow(&on_laptop, &frames[0], &cores[0], &scr, 4500, 100, 800, 600);
	initWindow(&on_middle, &frames[1], &cores[1], &scr, 3800, 800, 600, 500);
	initWindow(&huge, &frames[2], &cores[2], &scr, 100, 100, 2000, 1300);
	initWindow(&maximized, &frames[3], &cores[3], &scr, 1920, 0, 2560, 1440);
	maximized.flags.maximized = MAX_VERTICAL | MAX_HORIZONTAL;
	maximized.old_geometry.x = 2000;
	maximized.old_geometry.y = 50;
	maximized.old_geometry.width = 600;
	maximized.old_geometry.height = 400;

	/* Undock: only the laptop remains, now at the origin */
	old = state.heads;
	setHeads(&scr, &state, 1, laptop_names, laptop_rects);
	wRandR_FollowHeads(&scr, &state, &old, True);
	assert(dock_head == 0 && dock.on_right_side && dock.x_pos == 1920 - 64 && dock.y_pos == 0);
	assert(clip.x_pos == 0 && clip.y_pos == 1200 - 64);
	/* Windows follow the laptop panel or move fully onto it */
	assert(on_laptop.frame_x == 20 && on_laptop.frame_y == 100);
	assert(on_middle.frame_x == 1240 && on_middle.frame_y == 560);
	assert(huge.frame_x == 0 && huge.frame_y == 0);
	assert(cores[2].width == 1920 && cores[2].height == 1200);
	assert(maximized_calls == 1 && maximized_head == 0);
	assert(maximized.old_geometry.x == 2000 - 1920);

	/* Clip moved by the user to the bottom right corner of the laptop */
	clip.x_pos = 1920 - 64;
	clip.y_pos = 1200 - 64;

	/* Redock: the docked placement comes back, windows follow the laptop */
	old = state.heads;
	setHeads(&scr, &state, 3, dock_names, dock_rects);
	wRandR_FollowHeads(&scr, &state, &old, True);
	assert(dock_head == 0 && dock.x_pos == 6400 - 64 && dock.y_pos == 0);
	assert(clip.x_pos == 1920 && clip.y_pos == 1440 - 64);
	assert(on_laptop.frame_x == 4500 && on_laptop.frame_y == 100);

	/* Undock again: the laptop placement is remembered too, and the clip in
	 * the corner is moved so that its icons stay visible */
	old = state.heads;
	setHeads(&scr, &state, 1, laptop_names, laptop_rects);
	wRandR_FollowHeads(&scr, &state, &old, True);
	assert(clip.x_pos == 1920 - 11 * 64 && clip.y_pos == 1200 - 64);

	/* Placements survive a restart through WMState */
	{
		WMPropList *saved;
		WRandRState restarted = { 0 };
		WRandRAnchor d, c;

		scr.randr_state = &state;
		saved = WMDeepCopyPropList(wRandRGetPlacements(&scr));

		restarted.heads = state.heads;
		scr.randr_state = &restarted;
		wRandRSetPlacements(&scr, saved);
		memset(&d, 0, sizeof(d));
		memset(&c, 0, sizeof(c));
		setHeads(&scr, &restarted, 3, dock_names, dock_rects);
		wRandR_RecallPlacement(&restarted, &restarted.heads, &d, &c);
		assert(d.valid && d.right && strcmp(d.head, "eDP-1") == 0);
		assert(c.valid && strcmp(c.head, "DP-1-2") == 0 && c.dy == 1440 - 64);
		WMReleasePropList(saved);
		WMReleasePropList(restarted.placements);
		scr.randr_state = NULL;
	}

	/* A dock whose monitor disappears goes to the same side of the screen */
	old = state.heads;
	setHeads(&scr, &state, 3, dock_names, dock_rects);
	dock.x_pos = 1920 + 2560 - 64;
	dock.y_pos = 1440 - 64;
	{
		static const char *two_names[] = { "DP-1-3", "eDP-1" };
		static const int two_rects[][4] = { { 0, 0, 1920, 1080 }, { 1920, 0, 1920, 1200 } };

		WMReleasePropList(state.placements);
		state.placements = NULL;
		old = state.heads;
		setHeads(&scr, &state, 2, two_names, two_rects);
		wRandR_FollowHeads(&scr, &state, &old, True);
		assert(dock_head == 1 && dock.on_right_side && dock.x_pos == 3840 - 64);
		/* Bottom-anchored and kept with its icons on the head */
		assert(dock.y_pos == 1200 - 5 * 64);
	}

	/* Leaving fullscreen on a smaller monitor stays on that monitor */
	{
		static const char *big_names[] = { "DP-1" };
		static const int big_rects[][4] = { { 0, 0, 1920, 1080 } };
		static const char *small_names[] = { "HDMI-1" };
		static const int small_rects[][4] = { { 0, 0, 1280, 800 } };
		WWindow full;
		WFrameWindow frame;
		WCoreWindow core;

		scr.focused_window = NULL;
		initWindow(&full, &frame, &core, &scr, 0, 0, 1920, 1080);
		full.flags.fullscreen = True;
		full.bfs_geometry.x = 1600;
		full.bfs_geometry.y = 100;
		full.bfs_geometry.width = 300;
		full.bfs_geometry.height = 200;
		setHeads(&scr, &state, 1, big_names, big_rects);
		old = state.heads;
		setHeads(&scr, &state, 1, small_names, small_rects);
		wRandR_FollowHeads(&scr, &state, &old, True);
		assert(core.width == 1280 && core.height == 800);
		assert(full.bfs_geometry.x == 1280 - 320 && full.bfs_geometry.y == 100);
	}

	/* Windows clear of the dock stay clear of it on their new monitor */
	{
		static const char *two_names[] = { "DP-1", "DP-2" };
		static const int two_rects[][4] = { { 0, 0, 1920, 1080 }, { 1920, 0, 1920, 1080 } };
		static const char *one_names[] = { "DP-1" };
		static const int one_rects[][4] = { { 0, 0, 1920, 1080 } };
		const WMRect head = { { 0, 0 }, { 1000, 1000 } };
		const WRandRBox column = { 0, 0, 64, 1000 };
		WRandRBox box = { 10, 10, 960, 400 };
		WWindow clear, covering;
		WFrameWindow frame[2];
		WCoreWindow core[2];

		/* No position avoids the column, so the window shrinks beside it */
		wRandR_AvoidBoxes(&head, &column, 1, &box, True);
		assert(box.x == 64 && box.y == 10 && box.w == 936 && box.h == 400);
		box.x = 10;
		box.w = 960;
		wRandR_AvoidBoxes(&head, &column, 1, &box, False);
		assert(box.x == 10 && box.w == 960);

		wPreferences.flags.noclip = True;
		WMReleasePropList(state.placements);
		state.placements = NULL;
		scr.focused_window = NULL;
		setHeads(&scr, &state, 2, two_names, two_rects);
		dock.on_right_side = True;
		dock.x_pos = 3840 - 64;
		dock.y_pos = 0;
		initWindow(&clear, &frame[0], &core[0], &scr, 1220, 100, 700, 500);
		initWindow(&covering, &frame[1], &core[1], &scr, 3200, 100, 600, 500);
		old = state.heads;
		setHeads(&scr, &state, 1, one_names, one_rects);
		wRandR_FollowHeads(&scr, &state, &old, True);
		assert(dock.x_pos == 1920 - 64 && dock.y_pos == 0);
		assert(clear.frame_x == 1920 - 64 - 700 && clear.frame_y == 100);
		assert(core[0].width == 700 && core[0].height == 500);
		/* A window that was already over the dock is not pushed around */
		assert(covering.frame_x + 600 > 1920 - 64);
	}

	WMReleasePropList(state.placements);
	wfree(scr.xine_info.screens);
	wPreferences.flags.noclip = True;
}

int main(void)
{
	WScreen screen = { 0 };
	WRandRState state = { 0 };
	XRRModeInfo mode_info = { 0 };
	Screen xscreen = { 0 };
	int i;

	placementTests();

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
	wRandR_MarkOutputsAsRestored(&state);
	assert(!state.outputs[1].activate_pending && !state.outputs[2].activate_pending);
	state.outputs[1].activate_pending = state.outputs[2].activate_pending = True;

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
	assert(configured == 1 && loads && !applied);
	WMDeleteTimerHandler(screen.randr_debounce_timer);
	screen.randr_debounce_timer = NULL;

	/* Reconnecting a monitor set with a saved layout restores it instead. */
	output_info.crtc = None;
	output_info.connection = RR_Disconnected;
	loads = 0;
	wRandR_Update(&screen);
	assert(loads == 1 && !applied);
	output_info.connection = RR_Connected;
	saved_status = MONITOR_LAYOUT_LOADED;
	configured = 0;
	wRandR_Update(&screen);
	assert(loads == 2 && applied == 1 && configured == 0);
	assert(!state.outputs[0].activate_pending && !state.restore_pending);
	assert(screen.randr_debounce_timer);
	WMDeleteTimerHandler(screen.randr_debounce_timer);
	screen.randr_debounce_timer = NULL;
	/* Our own reconfiguration does not trigger another restore. */
	wRandR_Update(&screen);
	assert(loads == 2 && applied == 1);

	/* An output that vanishes entirely (DP-MST) triggers exactly one restore. */
	state.outputs[1] = state.outputs[0];
	state.outputs[1].xid = 99;
	state.outputs[1].crtc = None;
	state.n_outputs = 2;
	wRandR_Update(&screen);
	assert(loads == 3 && applied == 2 && state.n_outputs == 1);
	WMDeleteTimerHandler(screen.randr_debounce_timer);
	screen.randr_debounce_timer = NULL;
	wRandR_Update(&screen);
	assert(loads == 3 && applied == 2);

	/* A matching layout that is not ready yet is retried before falling back. */
	output_info.connection = RR_Disconnected;
	saved_status = MONITOR_LAYOUT_NONE;
	wRandR_Update(&screen);
	output_info.connection = RR_Connected;
	saved_status = MONITOR_LAYOUT_FAILED;
	loads = 0;
	for (i = 1; i < RANDR_RESTORE_ATTEMPTS; i++) {
		wRandR_Update(&screen);
		assert(loads == i && configured == 0 && state.restore_pending);
		assert(screen.randr_debounce_timer);
		WMDeleteTimerHandler(screen.randr_debounce_timer);
		screen.randr_debounce_timer = NULL;
	}
	wRandR_Update(&screen);
	assert(loads == RANDR_RESTORE_ATTEMPTS && configured == 1 && !state.restore_pending);
	WMDeleteTimerHandler(screen.randr_debounce_timer);
	screen.randr_debounce_timer = NULL;

	/* Without HotplugMonitor, connection changes never reconfigure outputs. */
	output_info.crtc = None;
	output_info.connection = RR_Disconnected;
	wPreferences.hotplug_monitor = False;
	wRandR_Update(&screen);
	output_info.connection = RR_Connected;
	loads = 0;
	wRandR_Update(&screen);
	assert(loads == 0 && configured == 1);

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
