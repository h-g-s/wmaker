/* dockedapp.c- docked application settings panel
 *
 *  Window Maker window manager
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

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "WindowMaker.h"
#include "window.h"
#include "icon.h"
#include "appicon.h"
#include "dock.h"
#include "dockedapp.h"
#include "dialog.h"
#include "misc.h"
#include "defaults.h"
#include "framewin.h"
#include "xinerama.h"


static void updateCommand(WAppIcon * icon, char *command)
{
	if (icon->command)
		wfree(icon->command);
	if (command && (command[0] == 0 || (command[0] == '-' && command[1] == 0))) {
		wfree(command);
		command = NULL;
	}
	icon->command = command;

	if (!icon->wm_class && !icon->wm_instance && icon->command && strlen(icon->command) > 0) {
		icon->forced_dock = 1;
	}
}

static void updatePasteCommand(WAppIcon * icon, char *command)
{
	if (icon->paste_command)
		wfree(icon->paste_command);
	if (command && (command[0] == 0 || (command[0] == '-' && command[1] == 0))) {
		wfree(command);
		command = NULL;
	}
	icon->paste_command = command;
}

#ifdef USE_DOCK_XDND
static void updateDNDCommand(WAppIcon * icon, char *command)
{
	if (icon->dnd_command)
		wfree(icon->dnd_command);
	if (command && (command[0] == 0 || (command[0] == '-' && command[1] == 0))) {
		wfree(command);
		command = NULL;
	}
	icon->dnd_command = command;
}
#endif	/* USE_DOCK_XDND */

static void updateSettingsPanelIcon(AppSettingsPanel * panel)
{
	char *file;
	int size;

	file = WMGetTextFieldText(panel->iconField);
	if (!file)
		WMSetLabelImage(panel->iconLabel, NULL);
	else {
		char *path;

		path = FindImage(wPreferences.icon_path, file);
		if (!path) {
			wwarning(_("could not find icon %s, used in a docked application"), file);
			wfree(file);
			WMSetLabelImage(panel->iconLabel, NULL);
			return;
		} else {
			WMPixmap *pixmap;
			RColor color;

			color.red = 0xae;
			color.green = 0xaa;
			color.blue = 0xae;
			color.alpha = 0;
			size = wPreferences.icon_size;
			pixmap = WMCreateScaledBlendedPixmapFromFile(WMWidgetScreen(panel->win), path, &color, size, size);
			if (!pixmap) {
				WMSetLabelImage(panel->iconLabel, NULL);
			} else {
				WMSetLabelImage(panel->iconLabel, pixmap);
				WMReleasePixmap(pixmap);
			}
		}
		wfree(file);
		wfree(path);
	}
}

static void chooseIconCallback(WMWidget * self, void *clientData)
{
	char *file;
	AppSettingsPanel *panel = (AppSettingsPanel *) clientData;
	int result;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	panel->choosingIcon = 1;

	WMSetButtonEnabled(panel->browseBtn, False);

	result = wIconChooserDialog(panel->wwin->screen_ptr, &file,
				    panel->editedIcon->wm_instance, panel->editedIcon->wm_class);

	panel->choosingIcon = 0;
	if (!panel->destroyed) {
		if (result) {
			WMSetTextFieldText(panel->iconField, file);
			updateSettingsPanelIcon(panel);
		}

		WMSetButtonEnabled(panel->browseBtn, True);
	} else {
		/* kluge for the case, the user asked to close the panel before
		 * the icon chooser */
		DestroyDockAppSettingsPanel(panel);
	}
	if (result)
		wfree(file);
}

static void panelBtnCallback(WMWidget * self, void *data)
{
	WMButton *btn = self;
	AppSettingsPanel *panel = (AppSettingsPanel *) data;
	char *text;

	if (panel->okBtn == btn) {
		text = WMGetTextFieldText(panel->iconField);
		if (text[0] == 0) {
			wfree(text);
			text = NULL;
		}

		if (!wIconChangeImageFile(panel->editedIcon->icon, text)) {
			char *buf;
			int len = strlen(text) + 64;

			buf = wmalloc(len);
			snprintf(buf, len, _("Could not open specified icon file: %s"), text);
			if (wMessageDialog(panel->wwin->screen_ptr, _("Error"), buf,
					   _("OK"), _("Ignore"), NULL) == WAPRDefault) {
				wfree(text);
				wfree(buf);
				return;
			}
			wfree(buf);
		} else {
			WAppIcon *aicon = panel->editedIcon;

			// Cf dock.c:dockIconPaint(WAppIcon *aicon)?
			if (aicon == aicon->icon->core->screen_ptr->clip_icon)
				wClipIconPaint(aicon);
			else if (wIsADrawer(aicon))
				wDrawerIconPaint(aicon);
			else
				wAppIconPaint(aicon);

			wDefaultChangeIcon(aicon->wm_instance, aicon->wm_class, text);
		}
		if (text)
			wfree(text);

		/* cannot free text from this, because it will be not be duplicated
		 * in updateCommand */
		text = WMGetTextFieldText(panel->commandField);
		if (text[0] == 0) {
			wfree(text);
			text = NULL;
		}
		updateCommand(panel->editedIcon, text);
#ifdef USE_DOCK_XDND
		/* cannot free text from this, because it will be not be duplicated
		 * in updateDNDCommand */
		text = WMGetTextFieldText(panel->dndCommandField);
		updateDNDCommand(panel->editedIcon, text);
#endif
		text = WMGetTextFieldText(panel->pasteCommandField);
		updatePasteCommand(panel->editedIcon, text);

		panel->editedIcon->auto_launch = WMGetButtonSelected(panel->autoLaunchBtn);

		panel->editedIcon->lock = WMGetButtonSelected(panel->lockBtn);
	}

	DestroyDockAppSettingsPanel(panel);
}

static void packSettingsWidget(WMBox *box, WMWidget *widget, WMSize size,
			       Bool horizontal, Bool expand, int spacing)
{
	int length;

	WMResizeWidget(widget, WMAX(size.width, 1), WMAX(size.height, 1));
	length = horizontal ? WMWidgetWidth(widget) : WMWidgetHeight(widget);
	WMAddBoxSubview(box, WMWidgetView(widget), expand, True, length, 0, spacing);
}

static void resizeSettingsFrame(XEvent *event, void *data)
{
	WMBox *content = data;

	if (event->type == ConfigureNotify) {
		WMPoint position = WMGetViewPosition(WMWidgetView(content));

		WMResizeWidget(content, WMAX(event->xconfigure.width - 2 * position.x, 1),
			       WMAX(event->xconfigure.height - position.y - 2, 1));
	}
}

static WMBox *createSettingsFrame(WMBox *parent, WMFrame **frame, const char *title, int padding)
{
	WMBox *content;
	WMFont *font = WMDefaultSystemFont(WMWidgetScreen(parent));

	*frame = WMCreateFrame(parent);
	WMSetFrameTitle(*frame, title);
	content = WMCreateBox(*frame);
	WMSetBoxBorderWidth(content, padding);
	WMMoveWidget(content, 2, WMFontHeight(font));
	WMReleaseFont(font);
	WMCreateEventHandler(WMWidgetView(*frame), StructureNotifyMask, resizeSettingsFrame, content);
	return content;
}

static void packSettingsFrame(WMBox *parent, WMFrame *frame, WMBox *content,
			      const char *title, int padding, int spacing)
{
	WMFont *font = WMDefaultSystemFont(WMWidgetScreen(parent));
	WMSize size = WMGetBoxMinimumSize(content);

	size.width = WMAX(size.width, WMWidthOfString(font, title, strlen(title)) + 2 * padding);
	WMResizeWidget(content, size.width, size.height);
	size.width += 4;
	size.height += WMFontHeight(font) + 2;
	WMReleaseFont(font);
	packSettingsWidget(parent, frame, size, False, False, spacing);
	WMMapSubwidgets(content);
	WMMapSubwidgets(frame);
}

void ShowDockAppSettingsPanel(WAppIcon * aicon)
{
	AppSettingsPanel *panel;
	WScreen *scr = aicon->icon->core->screen_ptr;
	Window parent;
	WMFont *font;
	int x, y;
	WMBox *vbox, *header, *content, *row;
	WMSize size, buttonSize, cancelSize;
	WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
	const char *title;
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;
	int iconSize, padding, spacing, titleHeight;

	/* get the width and height values of the system font for use with the WMScaleX/WMScaleY macros */
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);

	iconSize = wPreferences.icon_size;
	padding = WMAX(WMScaleY(10), 1);
	spacing = WMAX(WMScaleY(5), 1);
	titleHeight = WMFontHeight(scr->title_font) +
		2 * (wPreferences.window_title_clearance + TITLEBAR_EXTEND_SPACE);
	titleHeight = WMAX(wPreferences.window_title_min_height,
			   WMIN(titleHeight, wPreferences.window_title_max_height));

	panel = wmalloc(sizeof(AppSettingsPanel));

	panel->editedIcon = aicon;

	aicon->panel = panel;
	aicon->editing = 1;

	panel->win = WMCreateWindow(scr->wmscreen, "applicationSettings");
	vbox = WMCreateBox(panel->win);
	WMSetBoxBorderWidth(vbox, padding);

	header = WMCreateBox(vbox);
	WMSetBoxHorizontal(header, True);
	panel->iconLabel = WMCreateLabel(header);
	WMSetLabelImagePosition(panel->iconLabel, WIPImageOnly);
	packSettingsWidget(header, panel->iconLabel, wmksize(iconSize, iconSize), True, False, padding);

	panel->nameLabel = WMCreateLabel(header);
	font = WMDefaultBoldSystemFont(scr->wmscreen);
	WMSetLabelTextAlignment(panel->nameLabel, WALeft);
	WMSetLabelFont(panel->nameLabel, font);
	WMReleaseFont(font);
	if (aicon->wm_class && strcmp(aicon->wm_class, "DockApp") == 0)
		WMSetLabelText(panel->nameLabel, aicon->wm_instance);
	else
		WMSetLabelText(panel->nameLabel, aicon->wm_class);
	packSettingsWidget(header, panel->nameLabel, WMGetLabelPreferredSize(panel->nameLabel), True, True, 0);
	packSettingsWidget(vbox, header, WMGetBoxMinimumSize(header), False, False, padding);
	WMMapSubwidgets(header);

	panel->autoLaunchBtn = WMCreateSwitchButton(vbox);
	WMSetButtonText(panel->autoLaunchBtn, _("Start when Window Maker is started"));
	WMSetButtonSelected(panel->autoLaunchBtn, aicon->auto_launch);
	packSettingsWidget(vbox, panel->autoLaunchBtn, WMGetButtonPreferredSize(panel->autoLaunchBtn),
			   False, False, spacing);

	panel->lockBtn = WMCreateSwitchButton(vbox);
	WMSetButtonText(panel->lockBtn, _("Lock (prevent accidental removal)"));
	WMSetButtonSelected(panel->lockBtn, aicon->lock);
	packSettingsWidget(vbox, panel->lockBtn, WMGetButtonPreferredSize(panel->lockBtn), False, False, spacing);

	title = _("Application path and arguments");
	content = createSettingsFrame(vbox, &panel->commandFrame, title, padding);
	panel->commandField = WMCreateTextField(content);
	WMSetTextFieldText(panel->commandField, aicon->command);
	packSettingsWidget(content, panel->commandField, WMGetTextFieldPreferredSize(panel->commandField, 26),
			   False, False, 0);
	packSettingsFrame(vbox, panel->commandFrame, content, title, padding, spacing);

	title = _("Command for middle-click launch");
	content = createSettingsFrame(vbox, &panel->pasteCommandFrame, title, padding);
	panel->pasteCommandField = WMCreateTextField(content);
	WMSetTextFieldText(panel->pasteCommandField, aicon->paste_command);
	packSettingsWidget(content, panel->pasteCommandField,
			   WMGetTextFieldPreferredSize(panel->pasteCommandField, 26), False, False, spacing);
	panel->pasteCommandLabel = WMCreateLabel(content);
	WMSetLabelText(panel->pasteCommandLabel, _("%s will be replaced with current selection"));
	packSettingsWidget(content, panel->pasteCommandLabel, WMGetLabelPreferredSize(panel->pasteCommandLabel),
			   False, False, 0);
	packSettingsFrame(vbox, panel->pasteCommandFrame, content, title, padding, spacing);

	title = _("Command for dragged and dropped files");
	content = createSettingsFrame(vbox, &panel->dndCommandFrame, title, padding);
	panel->dndCommandField = WMCreateTextField(content);
	packSettingsWidget(content, panel->dndCommandField,
			   WMGetTextFieldPreferredSize(panel->dndCommandField, 26), False, False, spacing);
	panel->dndCommandLabel = WMCreateLabel(content);
#ifdef USE_DOCK_XDND
	WMSetTextFieldText(panel->dndCommandField, aicon->dnd_command);
	WMSetLabelText(panel->dndCommandLabel, _("%d will be replaced with the file name"));
#else
	WMSetTextFieldEditable(panel->dndCommandField, False);
	WMSetLabelText(panel->dndCommandLabel, _("XDnD support was not compiled in"));

	WMSetFrameTitleColor(panel->dndCommandFrame, WMDarkGrayColor(scr->wmscreen));
	WMSetLabelTextColor(panel->dndCommandLabel, WMDarkGrayColor(scr->wmscreen));
#endif
	packSettingsWidget(content, panel->dndCommandLabel, WMGetLabelPreferredSize(panel->dndCommandLabel),
			   False, False, 0);
	packSettingsFrame(vbox, panel->dndCommandFrame, content, title, padding, spacing);

	title = _("Icon Image");
	content = createSettingsFrame(vbox, &panel->iconFrame, title, padding);
	row = WMCreateBox(content);
	WMSetBoxHorizontal(row, True);
	panel->iconField = WMCreateTextField(row);
	WMSetTextFieldText(panel->iconField, wDefaultGetIconFile(aicon->wm_instance, aicon->wm_class, False));
	packSettingsWidget(row, panel->iconField, WMGetTextFieldPreferredSize(panel->iconField, 18),
			   True, True, spacing);

	panel->browseBtn = WMCreateCommandButton(row);
	WMSetButtonText(panel->browseBtn, _("Browse..."));
	WMSetButtonAction(panel->browseBtn, chooseIconCallback, panel);
	packSettingsWidget(row, panel->browseBtn, WMGetButtonPreferredSize(panel->browseBtn), True, False, 0);
	packSettingsWidget(content, row, WMGetBoxMinimumSize(row), False, False, 0);
	WMMapSubwidgets(row);
	packSettingsFrame(vbox, panel->iconFrame, content, title, padding, padding);

	{
		WMBox *hbox;

		hbox = WMCreateBox(vbox);
		WMSetBoxHorizontal(hbox, True);
		panel->okBtn = WMCreateCommandButton(hbox);
		WMSetButtonText(panel->okBtn, _("OK"));
		WMSetButtonAction(panel->okBtn, panelBtnCallback, panel);
		panel->cancelBtn = WMCreateCommandButton(hbox);
		WMSetButtonText(panel->cancelBtn, _("Cancel"));
		WMSetButtonAction(panel->cancelBtn, panelBtnCallback, panel);
		buttonSize = WMGetButtonPreferredSize(panel->okBtn);
		cancelSize = WMGetButtonPreferredSize(panel->cancelBtn);
		buttonSize.width = WMAX(WMScaleX(80), WMAX(buttonSize.width, cancelSize.width));
		buttonSize.height = WMAX(buttonSize.height, cancelSize.height);
		WMResizeWidget(panel->okBtn, buttonSize.width, buttonSize.height);
		WMResizeWidget(panel->cancelBtn, buttonSize.width, buttonSize.height);
		WMAddBoxSubviewAtEnd(hbox, WMWidgetView(panel->okBtn), False, True, buttonSize.width, 0, 0);
		WMAddBoxSubviewAtEnd(hbox, WMWidgetView(panel->cancelBtn), False, True, buttonSize.width, 0, spacing);
		packSettingsWidget(vbox, hbox, WMGetBoxMinimumSize(hbox), False, False, 0);

		WMMapSubwidgets(hbox);
	}

	size = WMGetBoxMinimumSize(vbox);
	size.width = WMAX(size.width, WMScaleX(300));
	WMResizeWidget(vbox, size.width, size.height);
	pwidth = WMIN(size.width, WMAX(64, (int)rect.size.width - 2 * (padding + scr->frame_border_width)));
	pheight = WMIN(size.height, WMAX(64, (int)rect.size.height - titleHeight -
					2 * (padding + scr->frame_border_width)));
	WMResizeWidget(panel->win, pwidth, pheight);
	if (pwidth < size.width || pheight < size.height) {
		WMScrollView *scroll = WMCreateScrollView(panel->win);

		WMResizeWidget(scroll, pwidth, pheight);
		WMSetScrollViewHasHorizontalScroller(scroll, True);
		WMSetScrollViewHasVerticalScroller(scroll, True);
		WMSetScrollViewContentView(scroll, WMWidgetView(vbox));
	}

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);
	WMMapSubwidgets(vbox);
	WMMapWidget(vbox);

	updateSettingsPanelIcon(panel);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);
	XSelectInput(dpy, parent, KeyPressMask | KeyReleaseMask);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	/*
	 * make things relative to head
	 */
	{
		y = aicon->y_pos;
		y = WMAX(rect.pos.y + padding,
			 WMIN(y, rect.pos.y + (int)rect.size.height - pheight - titleHeight -
			      padding - 2 * scr->frame_border_width));

		if (aicon->dock && aicon->dock->type == WM_DOCK) {
			if (aicon->dock->on_right_side)
				x = rect.pos.x + rect.size.width / 2;
			else
				x = rect.pos.x + rect.size.width / 2 - pwidth - WMScaleX(2);
		} else {
			x = rect.pos.x + (rect.size.width - pwidth) / 2;
		}
		x = WMAX(rect.pos.x, WMIN(x, rect.pos.x + (int)rect.size.width - pwidth -
					2 * scr->frame_border_width));
	}

	panel->wwin = wManageInternalWindow(scr, parent, None,
					    _("Docked Application Settings"), x, y, pwidth, pheight);

	panel->wwin->client_leader = WMWidgetXID(panel->win);

	panel->parent = parent;

	WMMapWidget(panel->win);

	wWindowMap(panel->wwin);
}

void DestroyDockAppSettingsPanel(AppSettingsPanel * panel)
{
	if (!panel->destroyed) {
		XUnmapWindow(dpy, panel->wwin->client_win);
		XReparentWindow(dpy, panel->wwin->client_win, panel->wwin->screen_ptr->root_win, 0, 0);
		wUnmanageWindow(panel->wwin, False, False);
	}

	panel->destroyed = 1;

	/*
	 * kluge. If we destroy the panel before the icon chooser is closed,
	 * we will crash when it does close, trying to access something in the
	 * destroyed panel. Could use wretain()/wrelease() in the panel,
	 * but it is not working for some reason.
	 */
	if (panel->choosingIcon)
		return;

	WMDestroyWidget(panel->win);

	XDestroyWindow(dpy, panel->parent);

	panel->editedIcon->panel = NULL;

	panel->editedIcon->editing = 0;

	wfree(panel);
}
