/* Paths.c- pixmap/icon paths
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "WPrefs.h"
#include "PanelLayout.h"
#include <unistd.h>
#include <assert.h>

typedef struct _Panel {
	WMBox *box;
	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMTabView *tabv;

	WMFrame *pixF;
	WMList *pixL;
	WMButton *pixaB;
	WMButton *pixrB;

	WMFrame *icoF;
	WMList *icoL;
	WMButton *icoaB;
	WMButton *icorB;

	WMColor *red;
	WMColor *black;
	WMColor *white;
	WMColor *gray;
	WMFont *font;
} _Panel;

#define ICON_FILE	"paths"

static void addPathToList(WMList * list, int index, const char *path)
{
	char *fpath = wexpandpath(path);
	WMListItem *item;

	item = WMInsertListItem(list, index, path);

	if (access(fpath, X_OK) != 0) {
		item->uflags = 1;
	}
	wfree(fpath);
}

static void showData(_Panel * panel)
{
	WMPropList *array, *val;
	int i;

	array = GetObjectForKey("IconPath");
	if (!array || !WMIsPLArray(array)) {
		if (array)
			wwarning(_("bad value in option IconPath. Using default path list"));
		addPathToList(panel->icoL, -1, "~/pixmaps");
		addPathToList(panel->icoL, -1, "~/" GSUSER_SUBDIR "/" USERDATA_SUBDIR "/Icons");
		addPathToList(panel->icoL, -1, "/usr/include/X11/pixmaps");
		addPathToList(panel->icoL, -1, "/usr/local/share/" PACKAGE_TARNAME "/Icons");
		addPathToList(panel->icoL, -1, "/usr/local/share/" PACKAGE_TARNAME "/Pixmaps");
		addPathToList(panel->icoL, -1, "/usr/share/" PACKAGE_TARNAME "/Icons");
	} else {
		for (i = 0; i < WMGetPropListItemCount(array); i++) {
			val = WMGetFromPLArray(array, i);
			addPathToList(panel->icoL, -1, WMGetFromPLString(val));
		}
	}

	array = GetObjectForKey("PixmapPath");
	if (!array || !WMIsPLArray(array)) {
		if (array)
			wwarning(_("bad value in option PixmapPath. Using default path list"));
		addPathToList(panel->pixL, -1, "~/pixmaps");
		addPathToList(panel->pixL, -1, "~/" GSUSER_SUBDIR "/" USERDATA_SUBDIR "/" PACKAGE_TARNAME "/Pixmaps");
		addPathToList(panel->pixL, -1, "/usr/local/share/" PACKAGE_TARNAME "/Pixmaps");
	} else {
		for (i = 0; i < WMGetPropListItemCount(array); i++) {
			val = WMGetFromPLArray(array, i);
			addPathToList(panel->pixL, -1, WMGetFromPLString(val));
		}
	}
}

static void pushButton(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int i;

	/* icon paths */
	if (w == panel->icorB) {
		i = WMGetListSelectedItemRow(panel->icoL);

		if (i >= 0)
			WMRemoveListItem(panel->icoL, i);
	}

	/* pixmap paths */
	if (w == panel->pixrB) {
		i = WMGetListSelectedItemRow(panel->pixL);

		if (i >= 0)
			WMRemoveListItem(panel->pixL, i);
	}
}

static void browseForFile(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	WMFilePanel *filePanel;

	assert(w == panel->icoaB || w == panel->pixaB);

	filePanel = WMGetOpenPanel(WMWidgetScreen(w));

	WMSetFilePanelCanChooseFiles(filePanel, False);

	if (WMRunModalFilePanelForDirectory(filePanel, panel->parent, "/", _("Select directory"), NULL) == True) {
		char *str = WMGetFilePanelFileName(filePanel);

		if (str) {
			int len = strlen(str);

			/* Remove the trailing '/' except if the path is exactly / */
			if (len > 1 && str[len - 1] == '/') {
				str[len - 1] = '\0';
				len--;
			}
			if (len > 0) {
				WMList *lPtr;
				int i;

				if (w == panel->icoaB)
					lPtr = panel->icoL;
				else if (w == panel->pixaB)
					lPtr = panel->pixL;
				else
					goto error_unknown_widget;

				i = WMGetListSelectedItemRow(lPtr);
				if (i >= 0)
					i++;
				addPathToList(lPtr, i, str);
				WMSetListBottomPosition(lPtr, WMGetListNumberOfRows(lPtr));
			}
		error_unknown_widget:
			wfree(str);
		}
	}
}

static void paintItem(WMList * lPtr, int index, Drawable d, char *text, int state, WMRect * rect)
{
	int width, height, x, y;
	_Panel *panel = (_Panel *) WMGetHangedData(lPtr);
	WMScreen *scr = WMWidgetScreen(lPtr);
	Display *dpy = WMScreenDisplay(scr);
	WMColor *backColor = (state & WLDSSelected) ? panel->white : panel->gray;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) index;

	width = rect->size.width;
	height = rect->size.height;
	x = rect->pos.x;
	y = rect->pos.y;

	XFillRectangle(dpy, d, WMColorGC(backColor), x, y, width, height);

	if (state & 1) {
		WMDrawString(scr, d, panel->red, panel->font, x + 4,
			     y + (height - WMFontHeight(panel->font)) / 2, text, strlen(text));
	} else {
		WMDrawString(scr, d, panel->black, panel->font, x + 4,
			     y + (height - WMFontHeight(panel->font)) / 2, text, strlen(text));
	}
}

static void storeData(_Panel * panel)
{
	WMPropList *list;
	WMPropList *tmp;
	int i;
	char *p;

	list = WMCreatePLArray(NULL, NULL);
	for (i = 0; i < WMGetListNumberOfRows(panel->icoL); i++) {
		p = WMGetListItem(panel->icoL, i)->text;
		tmp = WMCreatePLString(p);
		WMAddToPLArray(list, tmp);
		WMReleasePropList(tmp);
	}
	SetObjectForKey(list, "IconPath");
	WMReleasePropList(list);

	list = WMCreatePLArray(NULL, NULL);
	for (i = 0; i < WMGetListNumberOfRows(panel->pixL); i++) {
		p = WMGetListItem(panel->pixL, i)->text;
		tmp = WMCreatePLString(p);
		WMAddToPLArray(list, tmp);
		WMReleasePropList(tmp);
	}
	SetObjectForKey(list, "PixmapPath");
	WMReleasePropList(list);
}

static WMSize createPathPage(_Panel *panel, const char *title, int identifier,
			     WMFrame **frame, WMList **list, WMButton **add, WMButton **remove)
{
	WMTabViewItem *tab = WMCreateTabViewItemWithIdentifier(identifier);
	WMBox *column, *actions, *spacer;
	int rowHeight = WMFontHeight(panel->font) + 4;
	int listWidth = 42 * WMWidthOfString(panel->font, "M", 1) + 28;
	WMSize size;

	*frame = WMCreateFrame(panel->box);
	WMSetFrameRelief(*frame, WRFlat);
	WMSetTabViewItemView(tab, WMWidgetView(*frame));
	WMSetTabViewItemLabel(tab, title);
	WMAddItemInTabView(panel->tabv, tab);
	column = WMCreateBox(*frame);
	WMSetBoxBorderWidth(column, 10);
	*list = WMCreateList(column);
	WMSetListUserDrawProc(*list, paintItem);
	WMSetListUserDrawItemHeight(*list, rowHeight);
	WMHangData(*list, panel);
	WMPackBoxSubview(column, *list, wmksize(listWidth, rowHeight * 8 + 4), True, 10);
	actions = WMCreateBox(column);
	WMSetBoxHorizontal(actions, True);
	spacer = WMCreateBox(actions);
	WMPackBoxSubview(actions, spacer, wmksize(1, 1), True, 0);
	*add = WMCreateCommandButton(actions);
	WMSetButtonText(*add, _("Add"));
	WMSetButtonAction(*add, browseForFile, panel);
	WMSetButtonImagePosition(*add, WIPRight);
	*remove = WMCreateCommandButton(actions);
	WMSetButtonText(*remove, _("Remove"));
	WMSetButtonAction(*remove, pushButton, panel);
	WPrefsPackButton(actions, *add, 8);
	WPrefsPackButton(actions, *remove, 0);
	WMPackBoxSubview(column, actions, WMGetBoxMinimumSize(actions), False, 0);
	size = WMGetBoxMinimumSize(column);
	WMResizeWidget(*frame, size.width, size.height);
	WMSetViewExpandsToParent(WMWidgetView(column), 0, 0, 0, 0);
	WMMapSubwidgets(actions);
	WMMapSubwidgets(column);
	WMMapSubwidgets(*frame);
	return size;
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMSize icons, pixmaps, size;

	panel->white = WMWhiteColor(scr);
	panel->black = WMBlackColor(scr);
	panel->gray = WMGrayColor(scr);
	panel->red = WMCreateRGBColor(scr, 0xffff, 0, 0, True);
	panel->font = WMDefaultSystemFont(scr);

	panel->box = WPrefsCreatePanelBox(panel->parent);

	panel->tabv = WMCreateTabView(panel->box);
	WMSetTabViewFont(panel->tabv, panel->font);
	icons = createPathPage(panel, _("Icon Search Paths"), 0, &panel->icoF, &panel->icoL,
			       &panel->icoaB, &panel->icorB);
	pixmaps = createPathPage(panel, _("Pixmap Search Paths"), 1, &panel->pixF, &panel->pixL,
				 &panel->pixaB, &panel->pixrB);
	size = WMGetTabViewPreferredSize(panel->tabv,
				       wmksize(WMAX(icons.width, pixmaps.width), WMAX(icons.height, pixmaps.height)));
	WMPackBoxSubview(panel->box, panel->tabv, size, True, 0);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

Panel *InitPaths(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Search Path Configuration");

	panel->description = _("Search paths to use when looking for pixmaps\n" "and icons.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
