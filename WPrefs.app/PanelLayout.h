#ifndef WPREFS_PANEL_LAYOUT_H
#define WPREFS_PANEL_LAYOUT_H

#include <WINGs/WINGs.h>

typedef struct {
	WMFrame *frame;
	WMBox *content;
	const char *title;
} WPrefsGroup;

WMBox *WPrefsCreatePanelBox(WMWidget *parent);
WPrefsGroup WPrefsCreateGroup(WMWidget *parent, const char *title);
void WPrefsPackGroup(WMBox *parent, WPrefsGroup *group, Bool expand, int space);
void WPrefsPackButton(WMBox *parent, WMButton *button, int space);
void WPrefsPackLabel(WMBox *parent, WMLabel *label, int space);

typedef struct {
	WMWindow *window;
	WMScrollView *sections;
	WMScrollView *viewport;
	WMFrame *content;
	WMButton *help;
	WMButton *save;
	WMButton *close;
} WPrefsWindowLayout;

/* Called once after safe-to-measure panes have been constructed. */
void WPrefsArrangeWindow(WPrefsWindowLayout *layout, WMSize contentSize);

#endif
