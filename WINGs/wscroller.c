
/*
 * Until FreeBSD gets their act together;
 * http://www.mail-archive.com/freebsd-hackers@freebsd.org/msg69469.html
 */
#if defined( FREEBSD )
#   undef _XOPEN_SOURCE
#endif

#include "WINGsP.h"
#include "wconfig.h"

#include <math.h>

/* undefine will disable the autoadjusting of the knob dimple to be
 * directly below the cursor
 * DOES NOT WORK */
#undef STRICT_NEXT_BEHAVIOUR

#define AUTOSCROLL_INITIAL_DELAY 	200

#define AUTOSCROLL_DELAY 		40

const char *WMScrollerDidScrollNotification = "WMScrollerDidScrollNotification";

typedef struct W_Scroller {
	W_Class widgetClass;
	W_View *view;

	void *clientData;
	WMAction *action;

	float knobProportion;
	float floatValue;
	int thickness;
	WMPixmap *arrows[8];
	WMPixmap *dimple;

	WMHandlerID timerID;	/* for continuous scrolling mode */

#ifndef STRICT_NEXT_BEHAVIOUR
	int dragPoint;		/* point where the knob is being
				 * dragged */
#endif
	struct {
		WMScrollArrowPosition arrowsPosition:4;

		unsigned int horizontal:1;

		WMScrollerPart hitPart:4;

		/* */
		unsigned int documentFullyVisible:1;	/* document is fully visible */

		unsigned int prevSelected:1;

		unsigned int pushed:1;

		unsigned int incrDown:1;	/* whether increment button is down */

		unsigned int decrDown:1;

		unsigned int draggingKnob:1;

		unsigned int configured:1;

		unsigned int redrawPending:1;
	} flags;
} Scroller;

#define DEFAULT_ARROWS_POSITION	WSAMinEnd

#define BUTTON_SIZE             (sPtr->thickness - 4)

static void destroyScroller(Scroller * sPtr);
static void paintScroller(Scroller * sPtr);

static void willResizeScroller(W_ViewDelegate * self, WMView * view, unsigned int *width, unsigned int *height);
static void handleEvents(XEvent * event, void *data);
static void handleActionEvents(XEvent * event, void *data);

static void handleMotion(Scroller * sPtr, int mouseX, int mouseY);

W_ViewDelegate _ScrollerViewDelegate = {
	NULL,
	NULL,
	NULL,
	NULL,
	willResizeScroller
};

static WMPixmap *scrollerSymbol(WMScreen *screen, WMPixmap *source, int thickness, int kind)
{
	/* Supersample geometry, not the original small bitmap, for smooth edges. */
	const int samples = 4;
	RImage *image, *scaled;
	WMPixmap *pixmap;
	Pixmap canvas;
	GC gc;
	int width, height, w, h;

	if (thickness == SCROLLER_WIDTH)
		return WMRetainPixmap(source);
	width = kind == 8 ? 7 * thickness / SCROLLER_WIDTH : (thickness - 4) * 2 / 3;
	height = width;
	w = width * samples;
	h = height * samples;
	canvas = XCreatePixmap(screen->display, screen->rootWin, w, h, screen->depth);
	gc = XCreateGC(screen->display, canvas, 0, NULL);
	XSetForeground(screen->display, gc, W_PIXEL(kind >= 4 && kind < 8 ? screen->white : screen->gray));
	XFillRectangle(screen->display, canvas, gc, 0, 0, w, h);
	if (kind == 8) {
		int inset = samples;

		XSetForeground(screen->display, gc, W_PIXEL(screen->darkGray));
		XFillArc(screen->display, canvas, gc, 0, 0, w, h, 45 * 64, 180 * 64);
		XSetForeground(screen->display, gc, W_PIXEL(screen->white));
		XFillArc(screen->display, canvas, gc, 0, 0, w, h, 225 * 64, 180 * 64);
		XSetForeground(screen->display, gc, W_PIXEL(screen->gray));
		XFillArc(screen->display, canvas, gc, inset, inset, w - 2 * inset, h - 2 * inset, 0, 360 * 64);
	} else {
		XPoint points[3];
		int end = w - 1;
		int middle = end / 2;

		switch (kind % 4) {
		case 0:
			points[0] = (XPoint){ 0, middle };
			points[1] = (XPoint){ end, 0 };
			points[2] = (XPoint){ end, end };
			break;
		case 1:
			points[0] = (XPoint){ end, middle };
			points[1] = (XPoint){ 0, end };
			points[2] = (XPoint){ 0, 0 };
			break;
		case 2:
			points[0] = (XPoint){ middle, 0 };
			points[1] = (XPoint){ end, end };
			points[2] = (XPoint){ 0, end };
			break;
		default:
			points[0] = (XPoint){ middle, end };
			points[1] = (XPoint){ 0, 0 };
			points[2] = (XPoint){ end, 0 };
			break;
		}
		XSetForeground(screen->display, gc, W_PIXEL(kind >= 4 ? screen->darkGray : screen->black));
		XFillPolygon(screen->display, canvas, gc, points, 3, Convex, CoordModeOrigin);
	}
	image = RCreateImageFromDrawable(screen->rcontext, canvas, None);
	XFreeGC(screen->display, gc);
	XFreePixmap(screen->display, canvas);
	if (!image)
		goto failed;
	scaled = RSmoothScaleImage(image, width, height);
	RReleaseImage(image);
	if (!scaled)
		goto failed;
	pixmap = WMCreatePixmapFromRImage(screen, scaled, 127);
	RReleaseImage(scaled);
	if (pixmap)
		return pixmap;
failed:
	wwarning(_("could not render scrollbar symbol: %s"), RMessageForError(RErrorCode));
	return WMRetainPixmap(source);
}

int WMGetScrollerWidth(WMScroller *sPtr)
{
	CHECK_CLASS(sPtr, WC_Scroller);
	return sPtr->thickness;
}

WMScroller *WMCreateScroller(WMWidget * parent)
{
	Scroller *sPtr;
	WMScreen *screen = WMWidgetScreen(parent);
	WMFont *font = WMDefaultSystemFont(screen);
	int unit = WMAX(17, WMFontHeight(font));
	WMPixmap *sources[] = {
		screen->leftArrow, screen->rightArrow, screen->upArrow, screen->downArrow,
		screen->hiLeftArrow, screen->hiRightArrow, screen->hiUpArrow, screen->hiDownArrow
	};

	sPtr = wmalloc(sizeof(Scroller));
	sPtr->thickness = SCROLLER_WIDTH + SCROLLER_WIDTH * (unit - 17) / (2 * 17);
	WMReleaseFont(font);
	sPtr->widgetClass = WC_Scroller;

	sPtr->view = W_CreateView(W_VIEW(parent));
	if (!sPtr->view) {
		wfree(sPtr);
		return NULL;
	}
	sPtr->view->self = sPtr;
	for (int i = 0; i < 8; i++)
		sPtr->arrows[i] = scrollerSymbol(screen, sources[i], sPtr->thickness, i);
	sPtr->dimple = scrollerSymbol(screen, screen->scrollerDimple, sPtr->thickness, 8);

	sPtr->view->delegate = &_ScrollerViewDelegate;

	sPtr->flags.documentFullyVisible = 1;

	WMCreateEventHandler(sPtr->view, ExposureMask | StructureNotifyMask
			     | ClientMessageMask, handleEvents, sPtr);

	W_ResizeView(sPtr->view, sPtr->thickness, sPtr->thickness);
	sPtr->flags.arrowsPosition = DEFAULT_ARROWS_POSITION;

	WMCreateEventHandler(sPtr->view, ButtonPressMask | ButtonReleaseMask
			     | EnterWindowMask | LeaveWindowMask | ButtonMotionMask, handleActionEvents, sPtr);

	sPtr->flags.hitPart = WSNoPart;

	sPtr->floatValue = 0.0;
	sPtr->knobProportion = 1.0;

	return sPtr;
}

void WMSetScrollerArrowsPosition(WMScroller * sPtr, WMScrollArrowPosition position)
{
	sPtr->flags.arrowsPosition = position;
	if (sPtr->view->flags.realized) {
		paintScroller(sPtr);
	}
}

static void willResizeScroller(W_ViewDelegate * self, WMView * view, unsigned int *width, unsigned int *height)
{
	WMScroller *sPtr = (WMScroller *) view->self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	if (*width > *height) {
		sPtr->flags.horizontal = 1;
		*height = sPtr->thickness;
	} else {
		sPtr->flags.horizontal = 0;
		*width = sPtr->thickness;
	}
}

void WMSetScrollerAction(WMScroller * sPtr, WMAction * action, void *clientData)
{
	CHECK_CLASS(sPtr, WC_Scroller);

	sPtr->action = action;

	sPtr->clientData = clientData;
}

void WMSetScrollerParameters(WMScroller * sPtr, float floatValue, float knobProportion)
{
	/*
	 * This value represents 1 pixel on a 4k wide screen, it makes
	 * a good minimum; this ensure a non-null value to avoid
	 * potential division-by-0.
	 * Please note that there is another size check when drawing
	 * the knob to make sure it will remain selectable.
	 */
	static const float min_knob_proportion = 1.0 / 4096.0;

	CHECK_CLASS(sPtr, WC_Scroller);

	assert(!isnan(floatValue));

	if (floatValue < 0.0F)
		sPtr->floatValue = 0.0F;
	else if (floatValue > 1.0F)
		sPtr->floatValue = 1.0F;
	else
		sPtr->floatValue = floatValue;

	if (knobProportion <= min_knob_proportion) {

		sPtr->knobProportion = min_knob_proportion;
		sPtr->flags.documentFullyVisible = 0;

	} else if (knobProportion >= 1.0F) {

		sPtr->knobProportion = 1.0F;
		sPtr->flags.documentFullyVisible = 1;

	} else {
		sPtr->knobProportion = knobProportion;
		sPtr->flags.documentFullyVisible = 0;
	}

	if (sPtr->view->flags.realized)
		paintScroller(sPtr);

	/*   WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL); */
}

float WMGetScrollerKnobProportion(WMScroller * sPtr)
{
	CHECK_CLASS(sPtr, WC_Scroller);

	return sPtr->knobProportion;
}

float WMGetScrollerValue(WMScroller * sPtr)
{
	CHECK_CLASS(sPtr, WC_Scroller);

	return sPtr->floatValue;
}

WMScrollerPart WMGetScrollerHitPart(WMScroller * sPtr)
{
	CHECK_CLASS(sPtr, WC_Scroller);

	return sPtr->flags.hitPart;
}

static int arrowOffset(Scroller *sPtr, int part)
{
	int length = sPtr->flags.horizontal ? sPtr->view->size.width : sPtr->view->size.height;

	if (sPtr->flags.arrowsPosition == WSAMaxEnd)
		return length - (2 - part) * (BUTTON_SIZE + 1) - 1;
	return 2 + part * (BUTTON_SIZE + 1);
}

static void paintArrow(WMScroller * sPtr, Drawable d, int part)
/*
 * part- 0 paints the decrement arrow, 1 the increment arrow
 */
{
	WMView *view = sPtr->view;
	WMScreen *scr = view->screen;
	int ofs = arrowOffset(sPtr, part);
	W_Pixmap *arrow;

#ifndef DOUBLE_BUFFER
	GC gc = scr->lightGC;
#endif

	if (part == 0) {	/* decrement button */
		if (sPtr->flags.horizontal) {
			if (sPtr->flags.decrDown)
				arrow = sPtr->arrows[4];
			else
				arrow = sPtr->arrows[0];

		} else {
			if (sPtr->flags.decrDown)
				arrow = sPtr->arrows[6];
			else
				arrow = sPtr->arrows[2];
		}

#ifndef DOUBLE_BUFFER
		if (sPtr->flags.decrDown)
			gc = WMColorGC(scr->white);
#endif
	} else {		/* increment button */
		if (sPtr->flags.horizontal) {
			if (sPtr->flags.incrDown)
				arrow = sPtr->arrows[5];
			else
				arrow = sPtr->arrows[1];
		} else {
			if (sPtr->flags.incrDown)
				arrow = sPtr->arrows[7];
			else
				arrow = sPtr->arrows[3];
		}

#ifndef DOUBLE_BUFFER
		if (sPtr->flags.incrDown)
			gc = scr->whiteGC;
#endif
	}

	if (sPtr->flags.horizontal) {
		/* paint button */
#ifndef DOUBLE_BUFFER
		XFillRectangle(scr->display, d, gc, ofs + 1, 2 + 1, BUTTON_SIZE + 1 - 3, BUTTON_SIZE - 3);
#else
		if ((!part && sPtr->flags.decrDown) || (part && sPtr->flags.incrDown))
			XFillRectangle(scr->display, d, WMColorGC(scr->white),
				       ofs + 1, 2 + 1, BUTTON_SIZE + 1 - 3, BUTTON_SIZE - 3);
#endif				/* DOUBLE_BUFFER */
		W_DrawRelief(scr, d, ofs, 2, BUTTON_SIZE, BUTTON_SIZE, WRRaised);

		/* paint arrow */
		XSetClipMask(scr->display, scr->clipGC, arrow->mask);
		XSetClipOrigin(scr->display, scr->clipGC,
			       ofs + (BUTTON_SIZE - arrow->width) / 2, 2 + (BUTTON_SIZE - arrow->height) / 2);

		XCopyArea(scr->display, arrow->pixmap, d, scr->clipGC,
			  0, 0, arrow->width, arrow->height,
			  ofs + (BUTTON_SIZE - arrow->width) / 2, 2 + (BUTTON_SIZE - arrow->height) / 2);

	} else {		/* vertical */

		/* paint button */
#ifndef DOUBLE_BUFFER
		XFillRectangle(scr->display, d, gc, 2 + 1, ofs + 1, BUTTON_SIZE - 3, BUTTON_SIZE + 1 - 3);
#else
		if ((!part && sPtr->flags.decrDown) || (part && sPtr->flags.incrDown))
			XFillRectangle(scr->display, d, WMColorGC(scr->white),
				       2 + 1, ofs + 1, BUTTON_SIZE - 3, BUTTON_SIZE + 1 - 3);
#endif				/* DOUBLE_BUFFER */
		W_DrawRelief(scr, d, 2, ofs, BUTTON_SIZE, BUTTON_SIZE, WRRaised);

		/* paint arrow */

		XSetClipMask(scr->display, scr->clipGC, arrow->mask);
		XSetClipOrigin(scr->display, scr->clipGC,
			       2 + (BUTTON_SIZE - arrow->width) / 2, ofs + (BUTTON_SIZE - arrow->height) / 2);
		XCopyArea(scr->display, arrow->pixmap, d, scr->clipGC,
			  0, 0, arrow->width, arrow->height,
			  2 + (BUTTON_SIZE - arrow->width) / 2, ofs + (BUTTON_SIZE - arrow->height) / 2);
	}
}

static int knobLength(Scroller * sPtr)
{
	int tmp, length;

	if (sPtr->flags.horizontal)
		length = sPtr->view->size.width - 4;
	else
		length = sPtr->view->size.height - 4;

	if (sPtr->flags.arrowsPosition != WSANone) {
		length -= 2 * (BUTTON_SIZE + 1);
	}

	tmp = (int)((float)length * sPtr->knobProportion + 0.5F);
	/* keep minimum size */
	if (tmp < BUTTON_SIZE)
		tmp = BUTTON_SIZE;

	return WMAX(0, WMIN(tmp, length));
}

static void paintScroller(Scroller * sPtr)
{
	WMView *view = sPtr->view;
	WMScreen *scr = view->screen;
#ifdef DOUBLE_BUFFER
	Pixmap d;
#else
	Drawable d = view->window;
#endif
	int length, ofs;
	float knobP, knobL;

	if (view->size.width < 5 || view->size.height < 5)
		return;
#ifdef DOUBLE_BUFFER
	d = XCreatePixmap(scr->display, view->window, view->size.width, view->size.height, scr->depth);
	XFillRectangle(scr->display, d, WMColorGC(scr->gray), 0, 0, view->size.width, view->size.height);
#endif

	XDrawRectangle(scr->display, d, WMColorGC(scr->black), 0, 0, view->size.width - 1, view->size.height - 1);
#ifndef DOUBLE_BUFFER
	XDrawRectangle(scr->display, d, WMColorGC(scr->gray), 1, 1, view->size.width - 3, view->size.height - 3);
#endif

	if (sPtr->flags.horizontal)
		length = view->size.width - 4;
	else
		length = view->size.height - 4;

	if (sPtr->flags.documentFullyVisible ||
	    length < BUTTON_SIZE + (sPtr->flags.arrowsPosition == WSANone ? 0 : 2 * (BUTTON_SIZE + 1))) {
		XFillRectangle(scr->display, d, scr->stippleGC, 2, 2, view->size.width - 4, view->size.height - 4);
	} else {
		ofs = 2;
		if (sPtr->flags.arrowsPosition == WSAMaxEnd) {
			length -= (BUTTON_SIZE + 1) * 2;
		} else if (sPtr->flags.arrowsPosition == WSAMinEnd) {
			ofs += (BUTTON_SIZE + 1) * 2;
			length -= (BUTTON_SIZE + 1) * 2;
		}

		knobL = (float)knobLength(sPtr);

		knobP = sPtr->floatValue * ((float)length - knobL);

		if (sPtr->flags.horizontal) {
			/* before */
			XFillRectangle(scr->display, d, scr->stippleGC, ofs, 2, (int)knobP, view->size.height - 4);

			/* knob */
#ifndef DOUBLE_BUFFER
			XFillRectangle(scr->display, d, scr->lightGC,
				       ofs + (int)knobP + 2, 2 + 2, (int)knobL - 4, view->size.height - 4 - 4);
#endif
			W_DrawRelief(scr, d, ofs + (int)knobP, 2, (int)knobL, view->size.height - 4, WRRaised);

			XCopyArea(scr->display, sPtr->dimple->pixmap, d,
				  scr->copyGC, 0, 0,
				  sPtr->dimple->width, sPtr->dimple->height,
				  ofs + (int)knobP + ((int)knobL - sPtr->dimple->width - 1) / 2,
				  (view->size.height - sPtr->dimple->height - 1) / 2);

			/* after */
			if ((int)(knobP + knobL) < length)
				XFillRectangle(scr->display, d, scr->stippleGC,
					       ofs + (int)(knobP + knobL), 2,
					       length - (int)(knobP + knobL), view->size.height - 4);
		} else {
			/* before */
			if (knobP > 0.0F)
				XFillRectangle(scr->display, d, scr->stippleGC,
					       2, ofs, view->size.width - 4, (int)knobP);

			/* knob */
#ifndef DOUBLE_BUFFER
			XFillRectangle(scr->display, d, scr->lightGC,
				       2 + 2, ofs + (int)knobP + 2, view->size.width - 4 - 4, (int)knobL - 4);
#endif
			XCopyArea(scr->display, sPtr->dimple->pixmap, d,
				  scr->copyGC, 0, 0,
				  sPtr->dimple->width, sPtr->dimple->height,
				  (view->size.width - sPtr->dimple->width - 1) / 2,
				  ofs + (int)knobP + ((int)knobL - sPtr->dimple->height - 1) / 2);

			W_DrawRelief(scr, d, 2, ofs + (int)knobP, view->size.width - 4, (int)knobL, WRRaised);

			/* after */
			if ((int)(knobP + knobL) < length)
				XFillRectangle(scr->display, d, scr->stippleGC,
					       2, ofs + (int)(knobP + knobL),
					       view->size.width - 4, length - (int)(knobP + knobL));
		}

		if (sPtr->flags.arrowsPosition != WSANone) {
			paintArrow(sPtr, d, 0);
			paintArrow(sPtr, d, 1);
		}
	}

#ifdef DOUBLE_BUFFER
	XCopyArea(scr->display, d, view->window, scr->copyGC, 0, 0, view->size.width, view->size.height, 0, 0);
	XFreePixmap(scr->display, d);
#endif
}

static void handleEvents(XEvent * event, void *data)
{
	Scroller *sPtr = (Scroller *) data;

	CHECK_CLASS(data, WC_Scroller);

	switch (event->type) {
	case Expose:
		if (event->xexpose.count == 0)
			paintScroller(sPtr);
		break;

	case DestroyNotify:
		destroyScroller(sPtr);
		break;
	}
}

/*
 * locatePointInScroller-
 *     Return the part of the scroller where the point is located.
 */
static WMScrollerPart locatePointInScroller(Scroller * sPtr, int x, int y, int alternate)
{
	int width = sPtr->view->size.width;
	int height = sPtr->view->size.height;
	int c, p1, p2, p3, p4, p5, p6;
	int length = (sPtr->flags.horizontal ? width : height) - 4;
	int buttonSpan = BUTTON_SIZE + 1;
	int knobL, slotL;

	/* if there is no knob... */
	if (sPtr->flags.documentFullyVisible ||
	    length < BUTTON_SIZE + (sPtr->flags.arrowsPosition == WSANone ? 0 : 2 * buttonSpan))
		return WSKnobSlot;
	if (x < 0 || y < 0 || x >= width || y >= height)
		return WSNoPart;

	if (sPtr->flags.horizontal)
		c = x;
	else
		c = y;

	/*     p1  p2           p3           p4   p5   p6
	 * |   |   |###########|             |#####|   |   |
	 * | < | > |###########|      O      |#####| < | > |
	 * |   |   |###########|             |#####|   |   |
	 */

	if (sPtr->flags.arrowsPosition == WSAMinEnd) {
		p1 = arrowOffset(sPtr, 1);
		p2 = p1 + buttonSpan;
		slotL = length - 2 * buttonSpan;
		p5 = length + 2;
		p6 = p5;
	} else if (sPtr->flags.arrowsPosition == WSAMaxEnd) {
		slotL = length - 2 * buttonSpan;
		p5 = arrowOffset(sPtr, 0);
		p6 = arrowOffset(sPtr, 1);
		p1 = p2 = 2;
	} else {
		/* no arrows */
		p1 = p2 = 2;
		slotL = length;
		p5 = p6 = length + 2;
	}

	knobL = knobLength(sPtr);
	p3 = p2 + (int)((float)(slotL - knobL) * sPtr->floatValue);
	p4 = p3 + knobL;

	/* uses a mix of the NS and Win ways of doing scroll page */
	if (sPtr->flags.arrowsPosition == WSAMinEnd && c < p1)
		return alternate ? WSDecrementPage : WSDecrementLine;
	else if (sPtr->flags.arrowsPosition == WSAMinEnd && c < p2)
		return alternate ? WSIncrementPage : WSIncrementLine;
	else if (c < p3)
		return WSDecrementPage;
	else if (c < p4)
		return WSKnob;
	else if (c < p5 || sPtr->flags.arrowsPosition != WSAMaxEnd)
		return WSIncrementPage;
	else if (c < p6)
		return alternate ? WSDecrementPage : WSDecrementLine;
	else
		return alternate ? WSIncrementPage : WSIncrementLine;
}

static void handlePush(Scroller * sPtr, int pushX, int pushY, int alternate)
{
	WMScrollerPart part;
	int doAction = 0;

	part = locatePointInScroller(sPtr, pushX, pushY, alternate);

	sPtr->flags.hitPart = part;

	switch (part) {
	case WSIncrementLine:
		sPtr->flags.incrDown = 1;
		doAction = 1;
		break;

	case WSIncrementPage:
		doAction = 1;
		break;

	case WSDecrementLine:
		sPtr->flags.decrDown = 1;
		doAction = 1;
		break;

	case WSDecrementPage:
		doAction = 1;
		break;

	case WSKnob:
		sPtr->flags.draggingKnob = 1;
#ifndef STRICT_NEXT_BEHAVIOUR
		if (sPtr->flags.horizontal)
			sPtr->dragPoint = pushX;
		else
			sPtr->dragPoint = pushY;

		{
			int length, knobP;
			int buttonsLen;

			if (sPtr->flags.arrowsPosition != WSANone)
				buttonsLen = 2 * (BUTTON_SIZE + 1);
			else
				buttonsLen = 0;

			if (sPtr->flags.horizontal)
				length = sPtr->view->size.width - 4 - buttonsLen;
			else
				length = sPtr->view->size.height - 4 - buttonsLen;

			knobP = (int)(sPtr->floatValue * (float)(length - knobLength(sPtr)));

			if (sPtr->flags.arrowsPosition == WSAMinEnd)
				sPtr->dragPoint -= 2 + buttonsLen + knobP;
			else
				sPtr->dragPoint -= 2 + knobP;
		}
#endif				/* STRICT_NEXT_BEHAVIOUR */
		/* This does not seem necesary here since we don't know yet if the
		 * knob will be dragged later. -Dan
		 handleMotion(sPtr, pushX, pushY); */
		break;

	case WSDecrementWheel:
	case WSIncrementWheel:
	case WSKnobSlot:
	case WSNoPart:
		/* dummy */
		break;
	}

	if (doAction && sPtr->action) {
		(*sPtr->action) (sPtr, sPtr->clientData);

		WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
	}
}

static float floatValueForPoint(Scroller * sPtr, int point)
{
	float floatValue = 0;
	float position;
	int slotOfs, slotLength, knobL;

	if (sPtr->flags.horizontal)
		slotLength = sPtr->view->size.width - 4;
	else
		slotLength = sPtr->view->size.height - 4;

	slotOfs = 2;
	if (sPtr->flags.arrowsPosition == WSAMaxEnd) {
		slotLength -= (BUTTON_SIZE + 1) * 2;
	} else if (sPtr->flags.arrowsPosition == WSAMinEnd) {
		slotOfs += (BUTTON_SIZE + 1) * 2;
		slotLength -= (BUTTON_SIZE + 1) * 2;
	}

	knobL = (float)knobLength(sPtr);
	if (slotLength <= knobL)
		return sPtr->floatValue;
#ifdef STRICT_NEXT_BEHAVIOUR
	if (point < slotOfs + knobL / 2)
		position = (float)(slotOfs + knobL / 2);
	else if (point > slotOfs + slotLength - knobL / 2)
		position = (float)(slotOfs + slotLength - knobL / 2);
	else
		position = (float)point;

	floatValue = (position - (float)(slotOfs + slotLength / 2))
	    / (float)(slotLength - knobL);
#else
	/* Adjust the last point to lie inside the knob slot */
	if (point < slotOfs)
		position = (float)slotOfs;
	else if (point > slotOfs + slotLength)
		position = (float)(slotOfs + slotLength);
	else
		position = (float)point;

	/* Compute the float value */
	floatValue = (position - (float)slotOfs) / (float)(slotLength - knobL);
#endif

	assert(!isnan(floatValue));
	return floatValue;
}

static void handleMotion(Scroller * sPtr, int mouseX, int mouseY)
{
	if (sPtr->flags.draggingKnob) {
		float newFloatValue;
		int point;

		if (sPtr->flags.horizontal) {
			point = mouseX;
		} else {
			point = mouseY;
		}

#ifndef STRICT_NEXT_BEHAVIOUR
		point -= sPtr->dragPoint;
#endif

		newFloatValue = floatValueForPoint(sPtr, point);
		WMSetScrollerParameters(sPtr, newFloatValue, sPtr->knobProportion);
		if (sPtr->action) {
			(*sPtr->action) (sPtr, sPtr->clientData);
			WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
		}
	} else {
		int part;

		part = locatePointInScroller(sPtr, mouseX, mouseY, False);

		sPtr->flags.hitPart = part;

		if (part == WSIncrementLine && sPtr->flags.decrDown) {
			sPtr->flags.decrDown = 0;
			sPtr->flags.incrDown = 1;
		} else if (part == WSDecrementLine && sPtr->flags.incrDown) {
			sPtr->flags.incrDown = 0;
			sPtr->flags.decrDown = 1;
		} else if (part != WSIncrementLine && part != WSDecrementLine) {
			sPtr->flags.incrDown = 0;
			sPtr->flags.decrDown = 0;
		}
	}
}

static void autoScroll(void *clientData)
{
	Scroller *sPtr = (Scroller *) clientData;

	if (sPtr->action) {
		(*sPtr->action) (sPtr, sPtr->clientData);
		WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
	}
	sPtr->timerID = WMAddTimerHandler(AUTOSCROLL_DELAY, autoScroll, clientData);
}

static void handleActionEvents(XEvent * event, void *data)
{
	Scroller *sPtr = (Scroller *) data;
	int wheelDecrement, wheelIncrement;
	int id, dd;

	/* check if we're really dealing with a scroller, as something
	 * might have gone wrong in the event dispatching stuff */
	CHECK_CLASS(sPtr, WC_Scroller);

	id = sPtr->flags.incrDown;
	dd = sPtr->flags.decrDown;

	switch (event->type) {
	case EnterNotify:
		break;

	case LeaveNotify:
		if (sPtr->timerID) {
			WMDeleteTimerHandler(sPtr->timerID);
			sPtr->timerID = NULL;
		}
		sPtr->flags.incrDown = 0;
		sPtr->flags.decrDown = 0;
		break;

	case ButtonPress:
		/* FIXME: change Mod1Mask with something else */
		if (sPtr->flags.documentFullyVisible)
			break;

		if (sPtr->flags.horizontal) {
			wheelDecrement = WINGsConfiguration.mouseWheelDown;
			wheelIncrement = WINGsConfiguration.mouseWheelUp;
		} else {
			wheelDecrement = WINGsConfiguration.mouseWheelUp;
			wheelIncrement = WINGsConfiguration.mouseWheelDown;
		}

		if (event->xbutton.button == wheelDecrement) {
			if (event->xbutton.state & ControlMask) {
				sPtr->flags.hitPart = WSDecrementPage;
			} else if (event->xbutton.state & ShiftMask) {
				sPtr->flags.hitPart = WSDecrementLine;
			} else {
				sPtr->flags.hitPart = WSDecrementWheel;
			}
			if (sPtr->action) {
				(*sPtr->action) (sPtr, sPtr->clientData);
				WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
			}
		} else if (event->xbutton.button == wheelIncrement) {
			if (event->xbutton.state & ControlMask) {
				sPtr->flags.hitPart = WSIncrementPage;
			} else if (event->xbutton.state & ShiftMask) {
				sPtr->flags.hitPart = WSIncrementLine;
			} else {
				sPtr->flags.hitPart = WSIncrementWheel;
			}
			if (sPtr->action) {
				(*sPtr->action) (sPtr, sPtr->clientData);
				WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
			}
		} else {
			handlePush(sPtr, event->xbutton.x, event->xbutton.y, (event->xbutton.state & Mod1Mask)
				   || event->xbutton.button == Button2);
			/* continue scrolling if pushed on the buttons */
			if (sPtr->flags.hitPart == WSIncrementLine || sPtr->flags.hitPart == WSDecrementLine) {
				sPtr->timerID = WMAddTimerHandler(AUTOSCROLL_INITIAL_DELAY, autoScroll, sPtr);
			}
		}
		break;

	case ButtonRelease:
		if (sPtr->flags.draggingKnob) {
			if (sPtr->action) {
				(*sPtr->action) (sPtr, sPtr->clientData);
				WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
			}
		}
		if (sPtr->timerID) {
			WMDeleteTimerHandler(sPtr->timerID);
			sPtr->timerID = NULL;
		}
		sPtr->flags.incrDown = 0;
		sPtr->flags.decrDown = 0;
		sPtr->flags.draggingKnob = 0;
		break;

	case MotionNotify:
		handleMotion(sPtr, event->xbutton.x, event->xbutton.y);
		if (sPtr->timerID && sPtr->flags.hitPart != WSIncrementLine
		    && sPtr->flags.hitPart != WSDecrementLine) {
			WMDeleteTimerHandler(sPtr->timerID);
			sPtr->timerID = NULL;
		}
		break;
	}
	if (id != sPtr->flags.incrDown || dd != sPtr->flags.decrDown)
		paintScroller(sPtr);
}

static void destroyScroller(Scroller * sPtr)
{
	/* we don't want autoscroll try to scroll a freed widget */
	if (sPtr->timerID) {
		WMDeleteTimerHandler(sPtr->timerID);
	}

	for (int i = 0; i < 8; i++)
		WMReleasePixmap(sPtr->arrows[i]);
	WMReleasePixmap(sPtr->dimple);
	wfree(sPtr);
}
