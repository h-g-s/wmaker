/* WindowHandling.c- options for handling windows
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
#include "FontControls.h"

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMFrame *placF;
	WMPopUpButton *placP;
	WMLabel *porigL;
	WMLabel *porigvL;
	WMFrame *porigF;
	WMLabel *porigW;
	WMFrame *placementArea;
	int placementSliderSize;

	WMSlider *vsli;
	WMSlider *hsli;

	WMFrame *resF;
	WMSlider *resS;
	WMLabel *resL;
	WMButton *resaB;
	WMButton *resrB;

	WMFrame *maxiF;
	WMLabel *maxiL;
	WMButton *miconB;
	WMButton *mdockB;

	WMFrame *resizeF;
	WMLabel *resizeL;
	WMLabel *resizeTextL;
	WMSlider *resizeS;

	WMFrame *opaqF;
	WMButton *opaqB;
	WMButton *opaqresizeB;
	WMButton *opaqkeybB;

	WMFrame *dragmaxF;
	WMPopUpButton *dragmaxP;
} _Panel;

#define ICON_FILE "whandling"

#define OPAQUE_MOVE_PIXMAP "opaque"

#define NON_OPAQUE_MOVE_PIXMAP "nonopaque"

#define OPAQUE_RESIZE_PIXMAP "opaqueresize"

#define NON_OPAQUE_RESIZE_PIXMAP "noopaqueresize"

#define PLACEMENT_WINDOW_PIXMAP "smallwindow"

#define THUMB_SIZE	16

static const struct {
	const char *db_value;
	const char *label;
} window_placements[] = {
	{ "auto",     N_("Automatic") },
	{ "random",   N_("Random")    },
	{ "manual",   N_("Manual")    },
	{ "cascade",  N_("Cascade")   },
	{ "smart",    N_("Smart")     },
	{ "center",   N_("Center")    }
};

static const struct {
	const char *db_value;
	const char *label;
} drag_maximized_window_options[] = {
	{ "Move",            N_("...changes its position (normal behavior)") },
	{ "RestoreGeometry", N_("...restores its unmaximized geometry")      },
	{ "Unmaximize",      N_("...considers the window now unmaximized")   },
	{ "NoMove",          N_("...does not move the window")               }
};

static void sliderCallback(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int x, y, rx, ry;
	char buffer[64];
	int swidth = WMGetSliderMaxValue(panel->hsli);
	int sheight = WMGetSliderMaxValue(panel->vsli);

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	x = WMGetSliderValue(panel->hsli);
	y = WMGetSliderValue(panel->vsli);

	rx = x * (WMWidgetWidth(panel->porigF) - WMWidgetWidth(panel->porigW) - 4) / swidth + 2;
	ry = y * (WMWidgetHeight(panel->porigF) - WMWidgetHeight(panel->porigW) - 4) / sheight + 2;
	WMMoveWidget(panel->porigW, rx, ry);

	sprintf(buffer, "(%i,%i)", x, y);
	WMSetLabelText(panel->porigvL, buffer);
}

static void resistanceCallback(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	char buffer[64];
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	i = WMGetSliderValue(panel->resS);

	if (i == 0)
		WMSetLabelText(panel->resL, _("OFF"));
	else {
		sprintf(buffer, "%i", i);
		WMSetLabelText(panel->resL, buffer);
	}
}

static void resizeCallback(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	char buffer[64];
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	i = WMGetSliderValue(panel->resizeS);

	if (i == 0)
		WMSetLabelText(panel->resizeL, _("OFF"));
	else {
		sprintf(buffer, "%i", i);
		WMSetLabelText(panel->resizeL, buffer);
	}
}

static int getPlacement(const char *str)
{
	int i;

	if (!str)
		return 0;

	for (i = 0; i < wlengthof(window_placements); i++) {
		if (strcasecmp(str, window_placements[i].db_value) == 0)
			return i;
	}

	wwarning(_("bad option value %s in WindowPlacement. Using default value"), str);
	return 0;
}

static int getDragMaximizedWindow(const char *str)
{
	int i;

	if (!str)
		return 0;

	for (i = 0; i < wlengthof(drag_maximized_window_options); i++) {
		if (strcasecmp(str, drag_maximized_window_options[i].db_value) == 0)
			return i;
	}

	wwarning(_("bad option value %s in WindowPlacement. Using default value"), str);
	return 0;
}


static void showData(_Panel * panel)
{
	char *str;
	WMPropList *arr;
	int x, y;

	str = GetStringForKey("WindowPlacement");

	WMSetPopUpButtonSelectedItem(panel->placP, getPlacement(str));

	arr = GetObjectForKey("WindowPlaceOrigin");

	x = 0;
	y = 0;
	if (arr && (!WMIsPLArray(arr) || WMGetPropListItemCount(arr) != 2)) {
		wwarning(_("invalid data in option WindowPlaceOrigin. Using default (0,0)"));
	} else {
		if (arr) {
			x = atoi(WMGetFromPLString(WMGetFromPLArray(arr, 0)));
			y = atoi(WMGetFromPLString(WMGetFromPLArray(arr, 1)));
		}
	}

	WMSetSliderValue(panel->hsli, x);
	WMSetSliderValue(panel->vsli, y);

	sliderCallback(NULL, panel);

	x = GetIntegerForKey("EdgeResistance");
	WMSetSliderValue(panel->resS, x);
	resistanceCallback(NULL, panel);

	str = GetStringForKey("DragMaximizedWindow");
	WMSetPopUpButtonSelectedItem(panel->dragmaxP, getDragMaximizedWindow(str));

	x = GetIntegerForKey("ResizeIncrement");
	WMSetSliderValue(panel->resizeS, x);
	resizeCallback(NULL, panel);

	WMSetButtonSelected(panel->opaqB, GetBoolForKey("OpaqueMove"));
	WMSetButtonSelected(panel->opaqresizeB, GetBoolForKey("OpaqueResize"));
	WMSetButtonSelected(panel->opaqkeybB, GetBoolForKey("OpaqueMoveResizeKeyboard"));

	WMSetButtonSelected(panel->miconB, GetBoolForKey("NoWindowOverIcons"));
	WMSetButtonSelected(panel->mdockB, GetBoolForKey("NoWindowOverDock"));

	if (GetBoolForKey("Attraction"))
		WMPerformButtonClick(panel->resrB);
	else
		WMPerformButtonClick(panel->resaB);
}

static void storeData(_Panel * panel)
{
	WMPropList *arr;
	WMPropList *x, *y;
	char buf[16];

	SetBoolForKey(WMGetButtonSelected(panel->miconB), "NoWindowOverIcons");
	SetBoolForKey(WMGetButtonSelected(panel->mdockB), "NoWindowOverDock");

	SetBoolForKey(WMGetButtonSelected(panel->opaqB), "OpaqueMove");
	SetBoolForKey(WMGetButtonSelected(panel->opaqresizeB), "OpaqueResize");
	SetBoolForKey(WMGetButtonSelected(panel->opaqkeybB), "OpaqueMoveResizeKeyboard");

	SetStringForKey(window_placements[WMGetPopUpButtonSelectedItem(panel->placP)].db_value, "WindowPlacement");
	sprintf(buf, "%i", WMGetSliderValue(panel->hsli));
	x = WMCreatePLString(buf);
	sprintf(buf, "%i", WMGetSliderValue(panel->vsli));
	y = WMCreatePLString(buf);
	arr = WMCreatePLArray(x, y, NULL);
	WMReleasePropList(x);
	WMReleasePropList(y);
	SetObjectForKey(arr, "WindowPlaceOrigin");

	SetIntegerForKey(WMGetSliderValue(panel->resS), "EdgeResistance");

	SetStringForKey(drag_maximized_window_options[WMGetPopUpButtonSelectedItem(panel->dragmaxP)].db_value,
	                "DragMaximizedWindow");

	SetIntegerForKey(WMGetSliderValue(panel->resizeS), "ResizeIncrement");
	SetBoolForKey(WMGetButtonSelected(panel->resrB), "Attraction");

	WMReleasePropList(arr);
}

static WMPixmap *loadPreview(WMScreen *screen, const char *name)
{
	char *path = LocateImage(name);
	WMPixmap *image = NULL;

	if (path) {
		image = WMCreatePixmapFromFile(screen, path);
		if (!image)
			wwarning(_("could not load icon %s"), path);
		wfree(path);
	} else {
		wwarning(_("could not locate icon %s"), name);
	}
	return image;
}

static void createOpaqueButton(WMButton *button, WMFont *font, const char *normal, const char *selected)
{
	WMScreen *screen = WMWidgetScreen(button);
	WMPixmap *image = loadPreview(screen, normal);
	WMPixmap *altImage = loadPreview(screen, selected);

	WPrefsSetPreviewButtonFont(button, font, image, altImage);
	if (image)
		WMReleasePixmap(image);
	if (altImage)
		WMReleasePixmap(altImage);
}

static void packValueSlider(WMBox *parent, WMSlider *slider, WMLabel *label, WMFont *font, int maximum)
{
	WMSize size = wmksize(0, 0), current;
	char buffer[32];
	int i;

	/* Reserve every value, including translated OFF, before callbacks change the label. */
	for (i = 0; i <= maximum; i++) {
		sprintf(buffer, "%d", i);
		WMSetLabelText(label, i ? buffer : _("OFF"));
		current = WMGetLabelPreferredSize(label);
		size.width = WMAX(size.width, current.width);
		size.height = WMAX(size.height, current.height);
	}
	WMSetSliderFont(slider, font);
	WMPackBoxSubview(parent, slider, WMGetSliderPreferredSize(slider), True, 8);
	WMPackBoxSubview(parent, label, size, False, 0);
}

static void arrangePlacement(_Panel *panel)
{
	int swidth = WMGetSliderMaxValue(panel->hsli);
	int sheight = WMGetSliderMaxValue(panel->vsli);
	int thickness = panel->placementSliderSize;
	int availableWidth = WMAX(1, (int)WMWidgetWidth(panel->placementArea) - thickness - 2);
	int availableHeight = WMAX(1, (int)WMWidgetHeight(panel->placementArea) - thickness - 2);
	int width = availableWidth;
	int height = sheight * width / swidth;
	int x, y;

	if (height > availableHeight) {
		height = availableHeight;
		width = swidth * height / sheight;
	}
	width = WMAX(width, (int)WMWidgetWidth(panel->porigW) + 4);
	height = WMAX(height, (int)WMWidgetHeight(panel->porigW) + 4);
	x = WMAX(0, (availableWidth - width) / 2);
	y = WMAX(0, (availableHeight - height) / 2);
	WMResizeWidget(panel->porigF, width, height);
	WMMoveWidget(panel->porigF, x, y);
	WMResizeWidget(panel->hsli, width, thickness);
	WMMoveWidget(panel->hsli, x, y + height + 2);
	WMResizeWidget(panel->vsli, thickness, height);
	WMMoveWidget(panel->vsli, x + width + 2, y);
	sliderCallback(NULL, panel);
}

static void placementEvents(XEvent *event, void *data)
{
	if (event->type == DestroyNotify)
		WMRemoveNotificationObserver(data);
}

static void placementResized(void *data, WMNotification *notification)
{
	(void)notification;
	arrangePlacement(data);
}

static void createPlacement(_Panel *panel, WPrefsGroup *group, WMFont *font)
{
	WMScreen *screen = WMWidgetScreen(panel->parent);
	int swidth = WMScreenWidth(screen), sheight = WMScreenHeight(screen);
	int scale = WMAX(17, WMFontHeight(font));
	int width = 191 * scale / 17, height = 96 * scale / 17;
	WMBox *row = WMCreateBox(group->content);
	WMColor *color;
	WMPixmap *image;
	WMSize size;
	char buffer[64];

	WMSetBoxHorizontal(row, True);
	panel->placP = WMCreatePopUpButton(row);
	for (int i = 0; i < wlengthof(window_placements); i++)
		WMAddPopUpButtonItem(panel->placP, _(window_placements[i].label));
	WMPackBoxSubview(row, panel->placP, WMGetPopUpButtonPreferredSize(panel->placP), True, 8);
	panel->porigL = WMCreateLabel(row);
	WMSetLabelText(panel->porigL, _("Origin:"));
	WPrefsPackLabel(row, panel->porigL, 4);
	panel->porigvL = WMCreateLabel(row);
	sprintf(buffer, "(%d,%d)", swidth, sheight);
	WMSetLabelText(panel->porigvL, buffer);
	WMSetLabelTextAlignment(panel->porigvL, WACenter);
	WPrefsPackLabel(row, panel->porigvL, 0);
	WMPackBoxSubview(group->content, row, WMGetBoxMinimumSize(row), False, 10);
	WMMapSubwidgets(row);

	if (swidth * height / sheight <= width)
		width = swidth * height / sheight;
	else
		height = sheight * width / swidth;
	panel->placementArea = WMCreateFrame(group->content);
	WMSetFrameRelief(panel->placementArea, WRFlat);
	panel->porigF = WMCreateFrame(panel->placementArea);
	color = WMCreateRGBColor(screen, 0x5100, 0x5100, 0x7100, True);
	WMSetWidgetBackgroundColor(panel->porigF, color);
	WMReleaseColor(color);
	WMSetFrameRelief(panel->porigF, WRSunken);
	panel->porigW = WMCreateLabel(panel->porigF);
	image = loadPreview(screen, PLACEMENT_WINDOW_PIXMAP);
	if (image && scale > 17) {
		RImage *source = RCreateImageFromDrawable(WMScreenRContext(screen), image->pixmap, image->mask);
		RColor background = WMGetRColorFromColor(WMGetWidgetBackgroundColor(panel->porigF));
		RImage *scaled;

		if (source)
			RCombineImageWithColor(source, &background);
		scaled = source ? RSmoothScaleImage(source, image->width * scale / 17,
						   image->height * scale / 17) : NULL;
		WMPixmap *result = scaled ? WMCreatePixmapFromRImage(screen, scaled, 127) : NULL;

		if (source)
			RReleaseImage(source);
		if (scaled)
			RReleaseImage(scaled);
		if (!result)
			wwarning(_("could not scale placement preview: %s"), RMessageForError(RErrorCode));
		WMReleasePixmap(image);
		image = result;
	}
	if (image) {
		WMSetLabelImagePosition(panel->porigW, WIPImageOnly);
		WMSetLabelImage(panel->porigW, image);
		size = WMGetPixmapSize(image);
		WMReleasePixmap(image);
	} else {
		size = wmksize(THUMB_SIZE * scale / 17, THUMB_SIZE * scale / 17);
		WMSetLabelRelief(panel->porigW, WRRaised);
	}
	WMResizeWidget(panel->porigW, size.width, size.height);
	width = WMAX(width, (int)size.width + 4);
	height = WMAX(height, (int)size.height + 4);
	panel->vsli = WMCreateSlider(panel->placementArea);
	WMSetSliderFont(panel->vsli, font);
	size = WMGetSliderPreferredSize(panel->vsli);
	WMSetSliderMinValue(panel->vsli, 0);
	WMSetSliderMaxValue(panel->vsli, sheight);
	WMSetSliderAction(panel->vsli, sliderCallback, panel);
	panel->placementSliderSize = size.height;
	panel->hsli = WMCreateSlider(panel->placementArea);
	WMSetSliderFont(panel->hsli, font);
	WMSetSliderMinValue(panel->hsli, 0);
	WMSetSliderMaxValue(panel->hsli, swidth);
	WMSetSliderAction(panel->hsli, sliderCallback, panel);
	WMPackBoxSubview(group->content, panel->placementArea,
			 wmksize(width + size.height + 2, height + size.height + 2), True, 0);
	arrangePlacement(panel);
	WMAddNotificationObserver(placementResized, panel, WMViewSizeDidChangeNotification,
				  WMWidgetView(panel->placementArea));
	WMSetViewNotifySizeChanges(WMWidgetView(panel->placementArea), True);
	WMCreateEventHandler(WMWidgetView(panel->placementArea), StructureNotifyMask, placementEvents, panel);
	WMMapSubwidgets(panel->porigF);
	WMMapSubwidgets(panel->placementArea);
}

static void createPanel(Panel *p)
{
	_Panel *panel = (_Panel *)p;
	WMScreen *screen = WMWidgetScreen(panel->parent);
	WMFont *font = WMDefaultSystemFont(screen);
	WMBox *top, *left, *middle, *right, *row;
	WPrefsGroup placement, resistance, resize, maximize, opaque, dragging;

	panel->box = WPrefsCreatePanelBox(panel->parent);
	top = WMCreateBox(panel->box);
	WMSetBoxHorizontal(top, True);
	left = WMCreateBox(top);
	middle = WMCreateBox(top);
	right = WMCreateBox(top);

	placement = WPrefsCreateGroup(left, _("Window Placement"));
	panel->placF = placement.frame;
	WMSetBalloonTextForView(_("How to place windows when they are first put\n"
				  "on screen."), WMWidgetView(panel->placF));
	createPlacement(panel, &placement, font);
	WPrefsPackGroup(left, &placement, True, 0);

	resistance = WPrefsCreateGroup(middle, _("Edge Resistance"));
	panel->resF = resistance.frame;
	WMSetBalloonTextForView(_("Edge resistance will make windows `resist'\n"
				  "being moved further for the defined threshold\n"
				  "when moved against other windows or the edges\n"
				  "of the screen."), WMWidgetView(panel->resF));
	row = WMCreateBox(resistance.content);
	WMSetBoxHorizontal(row, True);
	panel->resS = WMCreateSlider(row);
	WMSetSliderMinValue(panel->resS, 0);
	WMSetSliderMaxValue(panel->resS, 80);
	WMSetSliderAction(panel->resS, resistanceCallback, panel);
	panel->resL = WMCreateLabel(row);
	packValueSlider(row, panel->resS, panel->resL, font, 80);
	WMPackBoxSubview(resistance.content, row, WMGetBoxMinimumSize(row), False, 8);
	WMMapSubwidgets(row);
	panel->resaB = WMCreateRadioButton(resistance.content);
	WMSetButtonText(panel->resaB, _("Resist"));
	panel->resrB = WMCreateRadioButton(resistance.content);
	WMSetButtonText(panel->resrB, _("Attract"));
	WMGroupButtons(panel->resrB, panel->resaB);
	WPrefsPackButton(resistance.content, panel->resaB, 8);
	WPrefsPackButton(resistance.content, panel->resrB, 0);
	WPrefsPackGroup(middle, &resistance, False, 12);

	resize = WPrefsCreateGroup(middle, _("Mod+Wheel"));
	panel->resizeF = resize.frame;
	panel->resizeTextL = WMCreateLabel(resize.content);
	WMSetLabelText(panel->resizeTextL, _("Resize increment:"));
	WPrefsPackLabel(resize.content, panel->resizeTextL, 8);
	row = WMCreateBox(resize.content);
	WMSetBoxHorizontal(row, True);
	panel->resizeS = WMCreateSlider(row);
	WMSetSliderMinValue(panel->resizeS, 0);
	WMSetSliderMaxValue(panel->resizeS, 100);
	WMSetSliderAction(panel->resizeS, resizeCallback, panel);
	panel->resizeL = WMCreateLabel(row);
	packValueSlider(row, panel->resizeS, panel->resizeL, font, 100);
	WMPackBoxSubview(resize.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);
	WPrefsPackGroup(middle, &resize, True, 0);

	maximize = WPrefsCreateGroup(right, _("When maximizing..."));
	panel->maxiF = maximize.frame;
	panel->maxiL = WMCreateLabel(maximize.content);
	WMSetLabelText(panel->maxiL, _("...do not cover:"));
	WPrefsPackLabel(maximize.content, panel->maxiL, 8);
	panel->miconB = WMCreateSwitchButton(maximize.content);
	WMSetButtonText(panel->miconB, _("Icons"));
	panel->mdockB = WMCreateSwitchButton(maximize.content);
	WMSetButtonText(panel->mdockB, _("The dock"));
	WPrefsPackButton(maximize.content, panel->miconB, 8);
	WPrefsPackButton(maximize.content, panel->mdockB, 0);
	WPrefsPackGroup(right, &maximize, False, 12);

	opaque = WPrefsCreateGroup(right, _("Opaque Move/Resize"));
	panel->opaqF = opaque.frame;
	WMSetBalloonTextForView(_("Whether the window contents or only a frame should\n"
				  "be displayed during a move or resize.\n"), WMWidgetView(panel->opaqF));
	row = WMCreateBox(opaque.content);
	WMSetBoxHorizontal(row, True);
	panel->opaqB = WMCreateButton(row, WBTToggle);
	createOpaqueButton(panel->opaqB, font, NON_OPAQUE_MOVE_PIXMAP, OPAQUE_MOVE_PIXMAP);
	panel->opaqresizeB = WMCreateButton(row, WBTToggle);
	createOpaqueButton(panel->opaqresizeB, font, NON_OPAQUE_RESIZE_PIXMAP, OPAQUE_RESIZE_PIXMAP);
	WPrefsPackButton(row, panel->opaqB, 8);
	WPrefsPackButton(row, panel->opaqresizeB, 0);
	WMPackBoxSubview(opaque.content, row, WMGetBoxMinimumSize(row), False, 8);
	WMMapSubwidgets(row);
	panel->opaqkeybB = WMCreateSwitchButton(opaque.content);
	WMSetButtonText(panel->opaqkeybB, _("by keyboard"));
	WMSetBalloonTextForView(_("When selected, moving or resizing windows\n"
				  "using keyboard shortcuts will also display its\n"
				  "content instead of just a frame."), WMWidgetView(panel->opaqkeybB));
	WPrefsPackButton(opaque.content, panel->opaqkeybB, 0);
	WPrefsPackGroup(right, &opaque, True, 0);
	WMPackBoxSubview(top, left, WMGetBoxMinimumSize(left), True, 12);
	WMPackBoxSubview(top, middle, WMGetBoxMinimumSize(middle), True, 12);
	WMPackBoxSubview(top, right, WMGetBoxMinimumSize(right), True, 0);
	WMPackBoxSubview(panel->box, top, WMGetBoxMinimumSize(top), True, 12);
	WMMapSubwidgets(left);
	WMMapSubwidgets(middle);
	WMMapSubwidgets(right);
	WMMapSubwidgets(top);

	dragging = WPrefsCreateGroup(panel->box, _("Dragging a maximized window..."));
	panel->dragmaxF = dragging.frame;
	panel->dragmaxP = WMCreatePopUpButton(dragging.content);
	for (int i = 0; i < wlengthof(drag_maximized_window_options); i++)
		WMAddPopUpButtonItem(panel->dragmaxP, _(drag_maximized_window_options[i].label));
	WMPackBoxSubview(dragging.content, panel->dragmaxP,
			 WMGetPopUpButtonPreferredSize(panel->dragmaxP), False, 0);
	WPrefsPackGroup(panel->box, &dragging, False, 0);
	WMReleaseFont(font);
	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);
	showData(panel);
}

static void undo(_Panel * panel)
{
	showData(panel);
}

Panel *InitWindowHandling(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Window Handling Preferences");

	panel->description = _("Window handling options. Initial placement style\n"
			       "edge resistance, opaque move etc.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.undoChanges = undo;

	AddSection(panel, ICON_FILE);

	return panel;
}
