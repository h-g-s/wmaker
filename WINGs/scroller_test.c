#include "wscroller.c"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static int actions;
static int drawnWidth;

static void testSymbols(WMScreen *screen, Scroller *scroller)
{
	for (int i = 0; i < 9; i++) {
		WMPixmap *pixmap = i == 8 ? scroller->dimple : scroller->arrows[i];
		RImage *image = RCreateImageFromDrawable(screen->rcontext, pixmap->pixmap, None);
		int stride, dark = 0, smooth = 0;
		long xsum = 0, ysum = 0;

		assert(image);
		stride = image->format == RRGBAFormat ? 4 : 3;
		for (unsigned int y = 0; y < image->height; y++)
			for (unsigned int x = 0; x < image->width; x++) {
				unsigned char *pixel = image->data + stride * (y * image->width + x);

				if (pixel[0] < 140 && pixel[1] < 140 && pixel[2] < 140) {
					dark++;
					xsum += x;
					ysum += y;
				}
				if (pixel[0] > 5 && pixel[0] < 135)
					smooth++;
			}
		assert(dark > 0);
		if (i < 4) {
			assert(smooth > 0);
			switch (i) {
			case 0:
				assert(xsum > dark * (image->width - 1) * 0.55);
				break;
			case 1:
				assert(xsum < dark * (image->width - 1) * 0.45);
				break;
			case 2:
				assert(ysum > dark * (image->height - 1) * 0.55);
				break;
			case 3:
				assert(ysum < dark * (image->height - 1) * 0.45);
				break;
			}
		} else if (i == 8) {
			long upper = 0, lower = 0;

			for (unsigned int y = 0; y < image->height / 2; y++)
				for (unsigned int x = 0; x < image->width; x++) {
					upper += image->data[stride * (y * image->width + x)];
					lower += image->data[stride * ((image->height - 1 - y) * image->width + x)];
				}
			assert(lower > upper);
		}
		RReleaseImage(image);
	}
}

static void didScroll(WMWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	actions++;
}

static void drawItem(WMList *list, int row, Drawable drawable, char *text,
		     int state, WMRect *rect)
{
	(void)list;
	(void)row;
	(void)drawable;
	(void)text;
	(void)state;
	drawnWidth = rect->size.width;
}

static WMScrollerPart hit(Scroller *scroller, int point, Bool alternate)
{
	return locatePointInScroller(scroller, scroller->flags.horizontal ? point : 5,
				     scroller->flags.horizontal ? 5 : point, alternate);
}

static void testScroller(WMWindow *window, Bool horizontal)
{
	Scroller *scroller = WMCreateScroller(window);
	WMScrollArrowPosition positions[] = { WSAMinEnd, WSAMaxEnd, WSANone };
	int thickness = WMGetScrollerWidth(scroller);
	int button = thickness - 4;
	int span = button + 1;
	int total = thickness * 10;

	WMResizeWidget(scroller, horizontal ? total : 1, horizontal ? 1 : total);
	assert(scroller->flags.horizontal == horizontal);
	assert((horizontal ? WMWidgetHeight(scroller) : WMWidgetWidth(scroller)) == thickness);
	WMRealizeWidget(scroller);
	WMSetScrollerAction(scroller, didScroll, NULL);
	for (unsigned int i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
		int offset = 2 + (positions[i] == WSAMinEnd ? 2 * span : 0);
		int length = total - 4 - (positions[i] == WSANone ? 0 : 2 * span);
		int knob, travel;

		WMSetScrollerArrowsPosition(scroller, positions[i]);
		WMSetScrollerParameters(scroller, 0, 0);
		assert(knobLength(scroller) == button);
		if (positions[i] != WSANone) {
			int arrow = arrowOffset(scroller, 0);

			assert(hit(scroller, arrow, False) == WSDecrementLine);
			assert(hit(scroller, arrow + span, False) == WSIncrementLine);
			assert(hit(scroller, arrow + button / 2, False) == WSDecrementLine);
			assert(hit(scroller, arrow + span + button / 2, False) == WSIncrementLine);
			assert(hit(scroller, arrow + button / 2, True) == WSDecrementPage);
			assert(hit(scroller, arrow + span + button / 2, True) == WSIncrementPage);
			actions = 0;
			handlePush(scroller, horizontal ? arrow + 5 : 5, horizontal ? 5 : arrow + 5, False);
			assert(actions == 1);
			assert(WMGetScrollerHitPart(scroller) == WSDecrementLine);
			paintScroller(scroller);
			scroller->flags.decrDown = 0;
		}
		WMSetScrollerParameters(scroller, 0.5, 0.25);
		knob = knobLength(scroller);
		travel = length - knob;
		assert(hit(scroller, offset + travel / 2 - 1, False) == WSDecrementPage);
		assert(hit(scroller, offset + travel / 2 + knob / 2, False) == WSKnob);
		assert(hit(scroller, offset + travel / 2 + knob + 1, False) == WSIncrementPage);
		assert(fabsf(floatValueForPoint(scroller, offset) - 0) < 0.001);
		assert(fabsf(floatValueForPoint(scroller, offset + travel) - 1) < 0.001);
		{
			int center = offset + travel / 2 + knob / 2;

			handlePush(scroller, horizontal ? center : 5, horizontal ? 5 : center, False);
			handleMotion(scroller, horizontal ? center + travel : 5,
				     horizontal ? 5 : center + travel);
			assert(WMGetScrollerValue(scroller) == 1);
			handleMotion(scroller, horizontal ? center - travel : 5,
				     horizontal ? 5 : center - travel);
			assert(WMGetScrollerValue(scroller) == 0);
			scroller->flags.draggingKnob = 0;
		}
		WMSetScrollerParameters(scroller, 1, 0.25);
		assert(hit(scroller, offset + travel, False) == WSKnob);
		WMSetScrollerParameters(scroller, 0, 1);
		assert(hit(scroller, offset, False) == WSKnobSlot);
		WMSetScrollerParameters(scroller, 0.5, 0.25);
		{
			int shortLength = button + 4 + (positions[i] == WSANone ? 0 : 2 * span);

			WMResizeWidget(scroller, horizontal ? shortLength : 1,
				       horizontal ? 1 : shortLength);
			assert(knobLength(scroller) == button);
			assert(floatValueForPoint(scroller, offset) == 0.5);
			WMResizeWidget(scroller, horizontal ? shortLength - 1 : 1,
				       horizontal ? 1 : shortLength - 1);
			assert(hit(scroller, 5, False) == WSKnobSlot);
		}
		WMResizeWidget(scroller, horizontal ? 2 : 1, horizontal ? 1 : 2);
		assert(scroller->flags.horizontal == horizontal);
		assert(hit(scroller, 1, False) == WSKnobSlot);
		assert(isfinite(floatValueForPoint(scroller, 1)));
		WMResizeWidget(scroller, horizontal ? total : 1, horizontal ? 1 : total);
	}
	WMDestroyWidget(scroller);
}

static void testContainers(WMWindow *window)
{
	WMScrollView *scroll = WMCreateScrollView(window);
	WMFrame *content = WMCreateFrame(window);
	WMList *list = WMCreateList(window);
	WMBrowser *browser = WMCreateBrowser(window);
	WMText *text = WMCreateText(window);
	WMScroller *vertical, *horizontal;
	WMRect rect;
	XEvent expose = { 0 };
	int thickness;

	WMSetScrollViewRelief(scroll, WRSunken);
	WMSetScrollViewHasVerticalScroller(scroll, True);
	WMSetScrollViewHasHorizontalScroller(scroll, True);
	vertical = WMGetScrollViewVerticalScroller(scroll);
	horizontal = WMGetScrollViewHorizontalScroller(scroll);
	thickness = WMGetScrollerWidth(vertical);
	WMResizeWidget(content, 1000, 1000);
	WMSetScrollViewContentView(scroll, WMWidgetView(content));
	WMResizeWidget(scroll, 300, 240);
	rect = WMGetScrollViewVisibleRect(scroll);
	assert(rect.size.width == 300 - 3 - thickness);
	assert(rect.size.height == 240 - 3 - thickness);
	assert(WMWidgetWidth(vertical) == thickness);
	assert(WMWidgetHeight(horizontal) == thickness);
	WMScrollViewScrollPoint(scroll, wmkpoint(1000 - rect.size.width, 1000 - rect.size.height));
	rect = WMGetScrollViewVisibleRect(scroll);
	assert(rect.pos.x + rect.size.width == 1000);
	assert(rect.pos.y + rect.size.height == 1000);
	WMResizeWidget(scroll, 1, 1);
	rect = WMGetScrollViewVisibleRect(scroll);
	assert(rect.size.width == 1 && rect.size.height == 1);
	assert(WMWidgetWidth(vertical) == thickness);
	assert(WMWidgetHeight(horizontal) == thickness);
	WMResizeScrollViewContent(scroll, 1, 1);
	rect = WMGetScrollViewVisibleRect(scroll);
	assert(rect.size.width == 1 && rect.size.height == 1);
	WMResizeWidget(scroll, 300, 240);
	WMSetScrollViewHasHorizontalScroller(scroll, False);
	WMSetScrollViewHasVerticalScroller(scroll, False);
	rect = WMGetScrollViewVisibleRect(scroll);
	assert(rect.size.width == 297 && rect.size.height == 237);

	WMResizeWidget(list, 300, 240);
	assert(WMWidgetWidth(WMGetListScroller(list)) == thickness);
	WMSetListUserDrawProc(list, drawItem);
	WMAddListItem(list, "Measured list row");
	WMRealizeWidget(list);
	WMMapWidget(list);
	expose.xexpose.type = Expose;
	expose.xexpose.display = WMWidgetScreen(window)->display;
	expose.xexpose.window = WMWidgetXID(list);
	XSync(expose.xexpose.display, False);
	while (XPending(expose.xexpose.display)) {
		XEvent event;

		XNextEvent(expose.xexpose.display, &event);
		WMHandleEvent(&event);
	}
	WMHandleEvent(&expose);
	assert(drawnWidth == 300 - thickness - 1);

	WMResizeWidget(browser, 400, 300);
	WMSetBrowserHasScroller(browser, False);
	WMSetBrowserHasScroller(browser, True);
	WMSetTextHasVerticalScroller(text, True);
	WMSetTextHasHorizontalScroller(text, True);
	WMResizeWidget(text, 300, 240);
	for (WMView *child = W_VIEW(text)->childrenList; child; child = child->nextSister)
		if (child->self && WMWidgetClass(child->self) == WC_Scroller)
			assert(child->size.width == thickness || child->size.height == thickness);
	WMDestroyWidget(scroll);
	WMDestroyWidget(list);
	WMDestroyWidget(browser);
	WMDestroyWidget(text);
}

int main(int argc, char **argv)
{
	Display *display;
	WMScreen *screen;
	int sizes[] = { 11, 24, 32, 11 };
	int previous = 0;

	if (!getenv("DISPLAY")) {
		puts("SKIP: scroller_test needs an X server");
		return 77;
	}
	WMInitializeApplication("scroller_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	for (unsigned int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMWindow *window;
		Scroller *scroller;
		int unit = WMAX(17, WMFontHeight(font));

		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		window = WMCreateWindow(screen, "scrollerTest");
		WMResizeWidget(window, 800, 600);
		WMRealizeWidget(window);
		WMMapWidget(window);
		scroller = WMCreateScroller(window);
		assert(WMGetScrollerWidth(scroller) >= 20);
		if (i == 0 || i == 3) {
			assert(WMGetScrollerWidth(scroller) == 20);
			assert(scroller->arrows[0] == screen->leftArrow);
		} else {
			assert(WMGetScrollerWidth(scroller) > previous);
			assert(scroller->arrows[0]->width > screen->leftArrow->width);
			assert(WMGetScrollerWidth(scroller) < 20 * unit / 17);
			assert(scroller->dimple->width > screen->scrollerDimple->width);
			testSymbols(screen, scroller);
		}
		previous = WMGetScrollerWidth(scroller);
		testScroller(window, False);
		testScroller(window, True);
		testContainers(window);
		WMDestroyWidget(window);
	}
	XSync(display, False);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware scrollbar tests passed");
	return 0;
}
