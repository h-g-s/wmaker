#include "WPrefs.h"
#include "PanelLayout.h"

WMBox *WPrefsCreatePanelBox(WMWidget *parent)
{
	WMBox *box = WMCreateBox(parent);

	WMSetBoxBorderWidth(box, 12);
	WMSetViewExpandsToParent(WMWidgetView(box), 2, 2, 2, 2);
	return box;
}

WPrefsGroup WPrefsCreateGroup(WMWidget *parent, const char *title)
{
	WPrefsGroup group;

	group.frame = WMCreateFrame(parent);
	group.title = title;
	WMSetFrameTitle(group.frame, title);
	group.content = WMCreateBox(group.frame);
	return group;
}

void WPrefsPackGroup(WMBox *parent, WPrefsGroup *group, Bool expand, int space)
{
	WMFont *font = WMDefaultSystemFont(WMWidgetScreen(parent));
	WMSize size = WMGetBoxMinimumSize(group->content);
	int top = WMFontHeight(font) + 8;
	int titleWidth = WMWidthOfString(font, group->title, strlen(group->title));

	size.width = WMAX(size.width + 20, titleWidth + 24);
	size.height += top + 10;
	WMResizeWidget(group->frame, size.width, size.height);
	WMSetViewExpandsToParent(WMWidgetView(group->content), 10, top, 10, 10);
	WMPackBoxSubview(parent, group->frame, size, expand, space);
	WMMapSubwidgets(group->content);
	WMMapSubwidgets(group->frame);
	WMReleaseFont(font);
}

void WPrefsPackButton(WMBox *parent, WMButton *button, int space)
{
	WMPackBoxSubview(parent, button, WMGetButtonPreferredSize(button), False, space);
}

void WPrefsPackLabel(WMBox *parent, WMLabel *label, int space)
{
	WMPackBoxSubview(parent, label, WMGetLabelPreferredSize(label), False, space);
}

typedef struct {
	WMScrollView *scroll;
	WMFrame *content;
	WMSize minimum;
} PanelViewport;

static void resizeViewport(PanelViewport *viewport)
{
	WMSize size = WMGetViewSize(WMWidgetView(viewport->scroll));
	Bool horizontal = viewport->minimum.width > size.width;
	Bool vertical = viewport->minimum.height > size.height;
	WMRect visible;

	WMSetScrollViewHasHorizontalScroller(viewport->scroll, horizontal);
	WMSetScrollViewHasVerticalScroller(viewport->scroll, vertical);
	/* Either scrollbar can make the other necessary. */
	for (int i = 0; i < 2; i++) {
		visible = WMGetScrollViewVisibleRect(viewport->scroll);
		if (!horizontal && viewport->minimum.width > visible.size.width) {
			horizontal = True;
			WMSetScrollViewHasHorizontalScroller(viewport->scroll, True);
		}
		if (!vertical && viewport->minimum.height > visible.size.height) {
			vertical = True;
			WMSetScrollViewHasVerticalScroller(viewport->scroll, True);
		}
	}
	visible = WMGetScrollViewVisibleRect(viewport->scroll);
	WMResizeWidget(viewport->content, WMAX(viewport->minimum.width, visible.size.width),
		       WMAX(viewport->minimum.height, visible.size.height));
}

static void viewportEvents(XEvent *event, void *data)
{
	if (event->type == DestroyNotify) {
		WMRemoveNotificationObserver(data);
		wfree(data);
	}
}

static void viewportResized(void *data, WMNotification *notification)
{
	(void)notification;
	resizeViewport(data);
}

void WPrefsArrangeWindow(WPrefsWindowLayout *layout, WMSize contentSize)
{
	WMScreen *screen = WMWidgetScreen(layout->window);
	WMBox *column = WMCreateBox(layout->window);
	WMBox *actions = WMCreateBox(column);
	WMSize footer, natural, maximum, visible;
	PanelViewport *viewport = wmalloc(sizeof(*viewport));

	if (W_VIEW(layout->window)->flags.realized)
		WMRealizeWidget(column);
	WMSetBoxBorderWidth(column, 10);
	WMSetBoxHorizontal(actions, True);
	WMReparentWidget(layout->help, actions, 0, 0);
	WMReparentWidget(layout->save, actions, 0, 0);
	WMReparentWidget(layout->close, actions, 0, 0);
	WMPackBoxSubview(actions, layout->help, WMGetButtonPreferredSize(layout->help), True, 12);
	WPrefsPackButton(actions, layout->save, 12);
	WPrefsPackButton(actions, layout->close, 0);
	footer = WMGetBoxMinimumSize(actions);

	WMReparentWidget(layout->sections, column, 0, 0);
	WMReparentWidget(layout->viewport, column, 0, 0);
	WMPackBoxSubview(column, layout->sections, wmksize(500, WMWidgetHeight(layout->sections)), False, 8);
	WMPackBoxSubview(column, layout->viewport, wmksize(contentSize.width + 20, 100), True, 8);
	WMPackBoxSubview(column, actions, footer, False, 0);
	natural = WMGetBoxMinimumSize(column);
	natural.height += contentSize.height + 20 - 100;
	maximum = wmksize(WMAX(1, (int)WMScreenWidth(screen) - 40),
			  WMAX(1, (int)WMScreenHeight(screen) - 80));
	visible = wmksize(WMIN(natural.width, maximum.width), WMIN(natural.height, maximum.height));
	WMSetWindowMinSize(layout->window, WMIN(visible.width, WMAX(360, footer.width + 20)),
			   WMIN(visible.height, 360));
	WMSetWindowMaxSize(layout->window, maximum.width, maximum.height);
	WMResizeWidget(layout->window, visible.width, visible.height);
	WMSetWindowInitialPosition(layout->window, (WMScreenWidth(screen) - visible.width) / 2,
				   (WMScreenHeight(screen) - visible.height) / 2);
	WMResizeWidget(column, visible.width, visible.height);
	WMSetViewExpandsToParent(WMWidgetView(column), 0, 0, 0, 0);

	viewport->scroll = layout->viewport;
	viewport->content = layout->content;
	viewport->minimum = contentSize;
	resizeViewport(viewport);
	WMSetViewNotifySizeChanges(WMWidgetView(layout->viewport), True);
	WMAddNotificationObserver(viewportResized, viewport, WMViewSizeDidChangeNotification,
				  WMWidgetView(layout->viewport));
	WMCreateEventHandler(WMWidgetView(layout->viewport), StructureNotifyMask, viewportEvents, viewport);
	WMMapSubwidgets(actions);
	WMMapSubwidgets(column);
	WMMapWidget(column);
}
