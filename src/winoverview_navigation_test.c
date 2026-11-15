#include "winoverview.c"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

Display *dpy;
struct WPreferences wPreferences;
struct wmaker_global_variables w_global;
WShortKey wKeyBindings[WKBD_LAST];

static KeySym test_keysym;
static unsigned int lookup_state;
static XEvent events[4];
static int event_count, event_index;

int XLookupString(XKeyEvent *event, char *buffer, int length, KeySym *symbol, XComposeStatus *status)
{
	lookup_state = event->state;
	*symbol = test_keysym;
	return 0;
}

int XClearArea(Display *display, Window window, int x, int y, unsigned int width,
	       unsigned int height, Bool exposures) { return 1; }
int XSetForeground(Display *display, GC gc, unsigned long foreground) { return 1; }
int XDrawRectangle(Display *display, Drawable drawable, GC gc, int x, int y,
		   unsigned int width, unsigned int height) { return 1; }

void WMMaskEvent(Display *display, long mask, XEvent *event)
{
	assert(event_index < event_count);
	*event = events[event_index++];
}

/* These window-management paths are not used by the navigation tests. */
WApplication *wApplicationOf(Window window) { assert(0); return NULL; }
WScreen *wScreenForRootWindow(Window window) { assert(0); return NULL; }
WWindow *wWindowFor(Window window) { assert(0); return NULL; }
int wGetHeadForPointerLocation(WScreen *scr) { assert(0); return 0; }
WMRect wGetRectForHead(WScreen *scr, int head) { assert(0); return (WMRect){ { 0, 0 }, { 0, 0 } }; }
WMArray *wSwitchPanelWindowList(WScreen *scr, Bool class_only) { assert(0); return NULL; }
char *ShrinkString(WMFont *font, const char *string, int width) { assert(0); return NULL; }
void wMakeWindowVisible(WWindow *wwin) { assert(0); }
void wSetFocusTo(WScreen *scr, WWindow *wwin) { assert(0); }
void wRaiseFrame(WCoreWindow *frame) { assert(0); }
void CommitStacking(WScreen *scr) { assert(0); }
void wWinPreviewActivate(WWindow *wwin) { assert(0); }
void wWinPreviewHide(void) { assert(0); }
Bool wSnapshotUsesComposite(void) { assert(0); return False; }
Bool wSnapshotCanGrab(WWindow *wwin, Window ignore) { assert(0); return False; }
Bool wSnapshotRenderScaled(WWindow *wwin, Drawable dest, int x, int y, int w, int h)
{ assert(0); return False; }
RImage *wSnapshotGrabWindow(WWindow *wwin, Bool raise) { assert(0); return NULL; }
RImage *wSnapshotScale(RImage *img, int w, int h) { assert(0); return NULL; }
RImage *wSnapshotScaleToFit(RImage *img, int w, int h) { assert(0); return NULL; }
void wSnapshotStore(WWindow *wwin, RImage *full) { assert(0); }
void wSnapshotUpdate(WWindow *wwin, Bool raise) { assert(0); }
void wSnapshotDraw(WScreen *scr, Drawable dest, RImage *img, int x, int y) { assert(0); }

static void setupGrid(void)
{
	static WScreen screen;
	int i;

	memset(&ov, 0, sizeof(ov));
	memset(wKeyBindings, 0, sizeof(wKeyBindings));
	ov.scr = &screen;
	ov.count = 6;
	ov.rect = (WMRect){ { 0, 0 }, { 1200, 800 } };
	ov.title_height = 22;
	for (i = 0; i < ov.count; i++) {
		ov.entries[i].nat_w = 640;
		/* Different rounding puts centers in the same row one pixel apart. */
		ov.entries[i].nat_h = i % 2 ? 479 : 480;
		ov.entries[i].client_win = i + 1;
	}
	layoutEntries();
	assert(ov.columns == 3);
}

static void testGridNavigation(void)
{
	int i;

	setupGrid();
	for (i = 0; i < ov.count; i++) {
		assert(neighbour(i, -1, 0) == (i + ov.count - 1) % ov.count);
		assert(neighbour(i, 1, 0) == (i + 1) % ov.count);
		assert(neighbour(i, 0, -1) == (i < 3 ? i : i - 3));
		assert(neighbour(i, 0, 1) == (i < 3 ? i + 3 : i));
		assert(neighbour(neighbour(i, 1, 0), -1, 0) == i);
		if (i < 3)
			assert(neighbour(neighbour(i, 0, 1), 0, -1) == i);
	}

	/* A short centered last row uses the nearest column in the next row. */
	ov.count = 5;
	layoutEntries();
	assert(ov.columns == 3);
	assert(neighbour(0, 0, 1) == 3);
	assert(neighbour(2, 0, 1) == 4);
	assert(neighbour(4, 0, -1) == 1);
	assert(neighbour(4, 0, 1) == 4);

	ov.count = ov.columns = 1;
	assert(neighbour(0, -1, 0) == 0);
	assert(neighbour(0, 1, 0) == 0);
	assert(neighbour(0, 0, -1) == 0);
	assert(neighbour(0, 0, 1) == 0);
}

static void press(KeySym symbol, unsigned int state)
{
	XEvent event = { 0 };

	event.xkey.type = KeyPress;
	event.xkey.keycode = 10;
	event.xkey.state = state;
	test_keysym = symbol;
	if (ov.cycling)
		handleCycleKey(&event);
	else
		handleKey(&event);
}

static void testKeyboardNavigation(Bool cycling)
{
	unsigned int held = cycling ? Mod1Mask : 0;

	setupGrid();
	ov.cycling = cycling;
	ov.hold_mask = held;
	w_global.shortcut.modifiers_mask = ~LockMask;
	press(XK_Down, held);
	assert(ov.selected == 3 && !ov.done);
	assert(lookup_state == 0);
	press(XK_Up, held);
	assert(ov.selected == 0 && !ov.done);
	press(XK_Right, held);
	assert(ov.selected == 1);
	press(XK_Left, held);
	assert(ov.selected == 0);
	press(XK_KP_Down, held | LockMask);
	assert(ov.selected == 3);
	press(XK_KP_Up, held);
	assert(ov.selected == 0);
	press(XK_KP_Right, held);
	press(XK_KP_Left, held);
	assert(ov.selected == 0);
	press(XK_Tab, held | ShiftMask);
	assert(ov.selected == 5 && lookup_state == ShiftMask);
	press(XK_Escape, held);
	assert(ov.done && ov.chosen == None);

	setupGrid();
	ov.cycling = cycling;
	ov.hold_mask = held;
	press(XK_Down, held);
	press(XK_Return, held);
	assert(ov.done && ov.chosen == ov.entries[3].client_win);
}

static void testCycleRelease(void)
{
	KeyCode modifiers[8] = { 0, 0, 0, 64, 0, 0, 0, 0 };
	XModifierKeymap modmap = { 1, modifiers };

	setupGrid();
	ov.cycling = True;
	ov.hold_mask = Mod1Mask;
	ov.modmap = &modmap;
	press(XK_Down, Mod1Mask);
	assert(ov.selected == 3);

	memset(events, 0, sizeof(events));
	events[0].xkey.type = KeyRelease;
	events[0].xkey.keycode = 10; /* releasing an arrow must not select */
	events[1].xkey.type = KeyRelease;
	events[1].xkey.keycode = 64;
	event_count = 2;
	event_index = 0;
	eventLoop();
	assert(event_index == 2);
	assert(ov.chosen == ov.entries[3].client_win);
	ov.modmap = NULL;
}

static void testCycleBindings(void)
{
	XEvent event = { 0 };

	setupGrid();
	ov.cycling = True;
	ov.hold_mask = Mod1Mask;
	wKeyBindings[WKBD_FOCUSNEXT].keycode = 23;
	wKeyBindings[WKBD_FOCUSNEXT].modifier = Mod1Mask;
	wKeyBindings[WKBD_FOCUSPREV].keycode = 23;
	wKeyBindings[WKBD_FOCUSPREV].modifier = Mod1Mask | ShiftMask;

	press(XK_Down, Mod1Mask);
	assert(ov.selected == 3);
	event.xkey.keycode = 23;
	event.xkey.state = Mod1Mask;
	handleCycleKey(&event);
	assert(ov.selected == 4);
	event.xkey.state |= ShiftMask;
	handleCycleKey(&event);
	assert(ov.selected == 3);
	press(XK_Up, Mod1Mask | ShiftMask);
	assert(ov.selected == 0 && lookup_state == ShiftMask);
}

int main(void)
{
	testGridNavigation();
	testKeyboardNavigation(False);
	testKeyboardNavigation(True);
	testCycleRelease();
	testCycleBindings();
	puts("Window overview navigation tests passed");
	return 0;
}
