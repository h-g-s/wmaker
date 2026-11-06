
/* MouseSettings.c- mouse options (some are equivalent to xset)
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
#include "FontControls.h"
#include "PanelLayout.h"

#include <X11/Xutil.h>
#include <X11/XKBlib.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>


#define XSET	"xset"


static const struct {
	const char *db_key;
	int default_action;
	enum { T_BUTTON, T_WHEEL } type;
	const char *display_label;
} button_list[] = {
	{ "MouseLeftButtonAction",     3, T_BUTTON, N_("Left Button") },
	{ "MouseMiddleButtonAction",   2, T_BUTTON, N_("Middle Button") },
	{ "MouseRightButtonAction",    1, T_BUTTON, N_("Right Button") },
	{ "MouseBackwardButtonAction", 0, T_BUTTON, N_("Back Button") },
	{ "MouseForwardButtonAction",  0, T_BUTTON, N_("Forward Button") },
	{ "MouseWheelAction",          0, T_WHEEL,  N_("Mouse Wheel") },
	{ "MouseWheelTiltAction",      0, T_WHEEL,  N_("Mouse Wheel Tilt") }
};

static const struct {
	const char *db_value;
	const char *label;
} button_actions[] = {
	{ "None",                  N_("None") },
	{ "OpenApplicationsMenu",  N_("Applications Menu") },
	{ "OpenWindowListMenu",    N_("Window List Menu") },
	{ "SelectWindows",         N_("Select Windows") },
	{ "MoveToPrevWorkspace",   N_("Previous Workspace") },
	{ "MoveToNextWorkspace",   N_("Next Workspace") },
	{ "MoveToPrevWindow",      N_("Previous Window") },
	{ "MoveToNextWindow",      N_("Next Window") }
};

static const struct {
	const char *db_value;
	const char *label;
} wheel_actions[] = {
	{ "None",              N_("None") },
	{ "SwitchWorkspaces",  N_("Switch Workspaces") },
	{ "SwitchWindows",     N_("Switch Windows") }
};

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMFrame *speedF;
	WMLabel *speedL;
	WMSlider *speedS;
	WMLabel *acceL;
	WMTextField *acceT;
	WMLabel *threL;
	WMTextField *threT;

	WMFrame *ddelaF;
	WMButton *ddelaB[5];
	WMTextField *ddelaT;
	WMLabel *ddelaL;
	DoubleTest *tester;

	WMFrame *menuF;
	struct {
		WMLabel *label;
		WMPopUpButton *popup;
	} mouse_action[wlengthof_nocheck(button_list)];

	WMButton *disaB;

	WMFrame *grabF;
	WMPopUpButton *grabP;

	 /**/ int maxThreshold;
	float acceleration;
} _Panel;

#define ICON_FILE "mousesettings"

#define SPEED_ICON_FILE "mousespeed"

/* need access to the double click variables */
#include <WINGs/WINGsP.h>

static char *modifierNames[8];

#define DELAY(i)		((i)*75+170)


static void setMouseAccel(WMScreen * scr, float accel, int threshold)
{
	int n, d;

	d = 10;
	n = accel * d;

	XChangePointerControl(WMScreenDisplay(scr), True, True, n, d, threshold);
}

static void speedChange(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int i;
	char buffer[64];
	int threshold;
	char *tmp;

	if (w == NULL) {
		float accel;

		tmp = WMGetTextFieldText(panel->acceT);
		if (sscanf(tmp, "%f", &accel) != 1 || accel < 0) {
			WMRunAlertPanel(WMWidgetScreen(panel->acceT), GetWindow(),
					_("Error"),
					_("Invalid mouse acceleration value. Must be a positive real value."),
					_("OK"), NULL, NULL);
			wfree(tmp);
			return;
		}
		panel->acceleration = accel;
		wfree(tmp);
	} else {
		i = (int)WMGetSliderValue(panel->speedS);

		panel->acceleration = 0.25 + (i * 0.25);

		sprintf(buffer, "%.2f", 0.25 + (i * 0.25));
		WMSetTextFieldText(panel->acceT, buffer);
	}

	tmp = WMGetTextFieldText(panel->threT);
	if (sscanf(tmp, "%i", &threshold) != 1 || threshold < 0 || threshold > panel->maxThreshold) {
		WMRunAlertPanel(WMWidgetScreen(panel->parent), GetWindow(), _("Error"),
				_
				("Invalid mouse acceleration threshold value. Must be the number of pixels to travel before accelerating."),
				_("OK"), NULL, NULL);
	} else {
		setMouseAccel(WMWidgetScreen(panel->parent), panel->acceleration, threshold);
	}
	wfree(tmp);
}

static void returnPressed(void *observerData, WMNotification * notification)
{
	_Panel *panel = (_Panel *) observerData;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) notification;

	speedChange(NULL, panel);
}

static void toggle_disabling_of_mouse_actions(WMWidget *w, void *client_data)
{
	WMButton *button = (WMButton *) w;
	Panel *panel = (Panel *) client_data;
	Bool do_enable;
	int i;

	if (WMGetButtonSelected(button))
		do_enable = False;
	else
		do_enable = True;

	for (i = 0; i < wlengthof(panel->mouse_action); i++)
		WMSetPopUpButtonEnabled(panel->mouse_action[i].popup, do_enable);
}

static void doubleClick(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int i;
	char buffer[32];

	for (i = 0; i < wlengthof(panel->ddelaB); i++) {
		if (panel->ddelaB[i] == w)
			break;
	}
	WINGsConfiguration.doubleClickDelay = DELAY(i);

	sprintf(buffer, "%i", DELAY(i));
	WMSetTextFieldText(panel->ddelaT, buffer);
}

static int getButtonAction(const char *str)
{
	int i;

	if (!str)
		return -2;

	for (i = 0; i < wlengthof(button_actions); i++) {
		if (strcasecmp(str, button_actions[i].db_value) == 0)
			return i;
	}

	return -1;
}

static int getWheelAction(const char *str)
{
	int i;

	if (!str)
		return -2;

	for (i = 0; i < wlengthof(wheel_actions); i++) {
		if (strcasecmp(str, wheel_actions[i].db_value) == 0)
			return i;
	}

	return -1;
}

static void getMouseParameters(Display * dpy, float *accel, int *thre)
{
	int n, d;

	XGetPointerControl(dpy, &n, &d, thre);

	*accel = (float)n / (float)d;
}

static void showData(_Panel * panel)
{
	char *str;
	int i;
	int a = -1, b = -1;
	float accel;
	char buffer[32];
	Display *dpy = WMScreenDisplay(WMWidgetScreen(panel->parent));

	for (i = 0; i < wlengthof(button_list); i++) {
		int action;

		str = GetStringForKey(button_list[i].db_key);
		if (button_list[i].type == T_BUTTON)
			action = getButtonAction(str);
		else
			action = getWheelAction(str);

		if (action < 0) {
			if (action == -1)
				wwarning(_("bad value %s for option %s"), str, button_list[i].db_key);
			action = button_list[i].default_action;
		}
		WMSetPopUpButtonSelectedItem(panel->mouse_action[i].popup, action);
	}

	WMSetButtonSelected(panel->disaB, GetBoolForKey("DisableWSMouseActions"));
	toggle_disabling_of_mouse_actions(panel->disaB, panel);

	 /**/ getMouseParameters(dpy, &accel, &a);
	panel->maxThreshold = WidthOfScreen(DefaultScreenOfDisplay(dpy));
	if (a > panel->maxThreshold) {
		panel->maxThreshold = a;
	}
	sprintf(buffer, "%i", a);
	WMSetTextFieldText(panel->threT, buffer);

	WMSetSliderValue(panel->speedS, (accel - 0.25F) / 0.25F);

	panel->acceleration = accel;
	sprintf(buffer, "%.2f", (double)accel);
	WMSetTextFieldText(panel->acceT, buffer);

	 /**/ b = GetIntegerForKey("DoubleClickTime");
	/* find best match */
	a = -1;
	for (i = 0; i < wlengthof(panel->ddelaB); i++) {
		if (DELAY(i) == b)
			a = i;
	}
	for (i = 0; i < wlengthof(panel->ddelaB); i++)
		WMSetButtonSelected(panel->ddelaB[i], False);
	if (a >= 0)
		WMSetButtonSelected(panel->ddelaB[a], True);
	sprintf(buffer, "%i", b);
	WMSetTextFieldText(panel->ddelaT, buffer);

	 /**/ str = GetStringForKey("ModifierKey");
	if (!str)
		str = "mod1";
	a = ModifierFromKey(dpy, str);

	if (a != -1) {
		str = modifierNames[a];

		a = 0;
		for (i = 0; i < WMGetPopUpButtonNumberOfItems(panel->grabP); i++) {
			if (strstr(WMGetPopUpButtonItem(panel->grabP, i), str)) {
				WMSetPopUpButtonSelectedItem(panel->grabP, i);
				a = 1;
				break;
			}
		}
	}

	if (a < 1) {
		char *previous;

		previous = WMGetPopUpButtonItem(panel->grabP, 0);
		if (previous != NULL)
			WMSetPopUpButtonSelectedItem(panel->grabP, 0);
		wwarning(_("modifier key %s for option ModifierKey was not recognized. Using %s as default"),
					str, previous?previous:"(empty)");
	}
}

static void fillModifierPopUp(WMPopUpButton * pop)
{
	XModifierKeymap *mapping;
	Display *dpy = WMScreenDisplay(WMWidgetScreen(pop));
	int i, j;
	char *str;
	char buffer[64];

	mapping = XGetModifierMapping(dpy);

	if (!mapping || mapping->max_keypermod == 0) {
		WMAddPopUpButtonItem(pop, "Mod1");
		WMAddPopUpButtonItem(pop, "Mod2");
		WMAddPopUpButtonItem(pop, "Mod3");
		WMAddPopUpButtonItem(pop, "Mod4");
		WMAddPopUpButtonItem(pop, "Mod5");
		wwarning(_("could not retrieve keyboard modifier mapping"));
		return;
	}

	for (j = 0; j < 8; j++) {
		int idx;
		char *array[8];
		int a;
		KeySym ksym;
		int k;
		char *ptr;
		char *tmp;

		a = 0;
		memset(array, 0, sizeof(char *) * 8);
		for (i = 0; i < mapping->max_keypermod; i++) {
			idx = i + j * mapping->max_keypermod;
			if (mapping->modifiermap[idx] != 0) {
				int l;
				for (l = 0; l < 4; l++) {
					ksym = W_KeycodeToKeysym(dpy, mapping->modifiermap[idx], l);
					if (ksym != NoSymbol)
						break;
				}
				if (ksym != NoSymbol)
					str = XKeysymToString(ksym);
				else
					str = NULL;
				if (str && !strstr(str, "_Lock") && !strstr(str, "Shift")
				    && !strstr(str, "Control")) {
					array[a++] = wstrdup(str);
				}
			}
		}

		for (k = 0; k < a; k++) {
			if (array[k] == NULL)
				continue;
			tmp = wstrdup(array[k]);
			ptr = strstr(tmp, "_L");
			if (ptr)
				*ptr = 0;
			ptr = strstr(tmp, "_R");
			if (ptr)
				*ptr = 0;
			sprintf(buffer, "%s (%s)", modifierNames[j], tmp);
			/*sprintf(buffer, "%s", tmp); */
			WMAddPopUpButtonItem(pop, buffer);
			for (i = k + 1; i < a; i++) {
				if (array[i] == NULL)
					continue;
				if (strstr(array[i], tmp)) {
					wfree(array[i]);
					array[i] = NULL;
					break;
				}
			}
			wfree(tmp);
		}

		while (--a > 0) {
			if (array[a])
				wfree(array[a]);
		}
	}

	if (mapping)
		XFreeModifiermap(mapping);
}

static void showPanel(_Panel *panel)
{
	for (int i = 0; i < wlengthof(panel->ddelaB); i++)
		if (WMGetButtonSelected(panel->ddelaB[i])) {
			WINGsConfiguration.doubleClickDelay = DELAY(i);
			break;
		}
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMFont *font = WMDefaultSystemFont(scr);
	WMColor *textColor = WMDarkGrayColor(scr);
	WPrefsGroup speed, grab, delay, actions;
	WMBox *columns, *left, *right, *row;
	WMPixmap *icon;
	int i, labelWidth = 0;
	int unit = WMAX(17, WMFontHeight(font));
	RColor color = WMGetRColorFromColor(WMGetWidgetBackgroundColor(panel->parent));
	char *path;

	panel->box = WPrefsCreatePanelBox(panel->parent);
	columns = WMCreateBox(panel->box);
	WMSetBoxHorizontal(columns, True);
	left = WMCreateBox(columns);
	right = WMCreateBox(columns);

    /**************** Mouse Speed ****************/
	speed = WPrefsCreateGroup(left, _("Mouse Speed"));
	panel->speedF = speed.frame;
	row = WMCreateBox(speed.content);
	WMSetBoxHorizontal(row, True);

	panel->speedL = WMCreateLabel(row);
	WMSetLabelImagePosition(panel->speedL, WIPImageOnly);
	path = LocateImage(SPEED_ICON_FILE);
	if (path) {
		icon = WMCreateBlendedPixmapFromFile(scr, path, &color);
		if (icon) {
			WPrefsSetImageLabelFont(panel->speedL, font, icon);
			WMReleasePixmap(icon);
		} else {
			wwarning(_("could not load icon %s"), path);
		}
		wfree(path);
	}
	WPrefsPackLabel(row, panel->speedL, 10);

	panel->speedS = WMCreateSlider(row);
	WMSetSliderMinValue(panel->speedS, 0);
	WMSetSliderMaxValue(panel->speedS, 40);
	WMSetSliderContinuous(panel->speedS, False);
	WMSetSliderAction(panel->speedS, speedChange, panel);
	WMPackBoxSubview(row, panel->speedS, wmksize(150 * unit / 17, 20 * unit / 17), True, 0);
	WMPackBoxSubview(speed.content, row, WMGetBoxMinimumSize(row), False, 10);
	WMMapSubwidgets(row);

	row = WMCreateBox(speed.content);
	WMSetBoxHorizontal(row, True);
	panel->acceL = WMCreateLabel(row);
	WMSetLabelTextAlignment(panel->acceL, WARight);
	WMSetLabelText(panel->acceL, _("Accel.:"));
	WPrefsPackLabel(row, panel->acceL, 8);

	panel->acceT = WMCreateTextField(row);
	WMPackBoxSubview(row, panel->acceT, WMGetTextFieldPreferredSize(panel->acceT, 5), False, 10);
	WMAddNotificationObserver(returnPressed, panel, WMTextDidEndEditingNotification, panel->acceT);

	panel->threL = WMCreateLabel(row);
	WMSetLabelTextAlignment(panel->threL, WARight);
	WMSetLabelText(panel->threL, _("Threshold:"));
	WPrefsPackLabel(row, panel->threL, 8);

	panel->threT = WMCreateTextField(row);
	WMPackBoxSubview(row, panel->threT, WMGetTextFieldPreferredSize(panel->threT, 5), False, 0);
	WMAddNotificationObserver(returnPressed, panel, WMTextDidEndEditingNotification, panel->threT);

	WMPackBoxSubview(speed.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);

	/* ************** Grab Modifier **************** */
	grab = WPrefsCreateGroup(left, _("Mouse Grab Modifier"));
	panel->grabF = grab.frame;

	WMSetBalloonTextForView(_("Keyboard modifier to use for actions that\n"
				  "involve dragging windows with the mouse,\n"
				  "clicking inside the window."), WMWidgetView(panel->grabF));

	panel->grabP = WMCreatePopUpButton(grab.content);

	fillModifierPopUp(panel->grabP);
	WMPackBoxSubview(grab.content, panel->grabP, WMGetPopUpButtonPreferredSize(panel->grabP), False, 0);
	WPrefsPackGroup(left, &grab, False, 12);
	WPrefsPackGroup(left, &speed, False, 12);

	/***************** Doubleclick Delay ****************/

	delay = WPrefsCreateGroup(left, _("Double-Click Delay"));
	panel->ddelaF = delay.frame;
	row = WMCreateBox(delay.content);
	WMSetBoxHorizontal(row, True);

	for (i = 0; i < wlengthof(panel->ddelaB); i++) {
		panel->ddelaB[i] = WMCreateCustomButton(row, WBBStateChangeMask);
		WPrefsSetClockButtonFont(panel->ddelaB[i], font, i + 1);
		WMSetButtonAction(panel->ddelaB[i], doubleClick, panel);
		if (i > 0) {
			WMGroupButtons(panel->ddelaB[0], panel->ddelaB[i]);
		}
		WMPackBoxSubview(row, panel->ddelaB[i], WMGetViewSize(WMWidgetView(panel->ddelaB[i])),
				 True, i == 4 ? 0 : 8);
	}
	WMPackBoxSubview(delay.content, row, WMGetBoxMinimumSize(row), False, 10);
	WMMapSubwidgets(row);
	row = WMCreateBox(delay.content);
	WMSetBoxHorizontal(row, True);
	panel->tester = CreateDoubleTest(row, _("Test"));
	WMPackBoxSubview(row, panel->tester, GetDoubleTestPreferredSize(panel->tester), True, 12);
	panel->ddelaT = WMCreateTextField(row);
	WMPackBoxSubview(row, panel->ddelaT, WMGetTextFieldPreferredSize(panel->ddelaT, 5), False, 8);
	panel->ddelaL = WMCreateLabel(row);
	WMSetLabelTextColor(panel->ddelaL, textColor);
	WMSetLabelText(panel->ddelaL, _("ms"));
	WPrefsPackLabel(row, panel->ddelaL, 0);
	WMPackBoxSubview(delay.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);
	row = WMCreateBox(delay.content);
	WMPackBoxSubview(delay.content, row, wmksize(1, 1), True, 0);
	WPrefsPackGroup(left, &delay, True, 0);

	/* ************** Workspace Action Buttons **************** */

	actions = WPrefsCreateGroup(right, _("Workspace Mouse Actions"));
	panel->menuF = actions.frame;

	panel->disaB = WMCreateSwitchButton(actions.content);
	WMSetButtonText(panel->disaB, _("Disable mouse actions"));
	WMSetButtonAction(panel->disaB, toggle_disabling_of_mouse_actions, panel);
	WPrefsPackButton(actions.content, panel->disaB, 10);
	for (i = 0; i < wlengthof(button_list); i++)
		labelWidth = WMAX(labelWidth, WMWidthOfString(font, _(button_list[i].display_label),
							    strlen(_(button_list[i].display_label))) + 8);

	for (i = 0; i < wlengthof(button_list); i++) {
		int j;
		WMSize size;

		row = WMCreateBox(actions.content);
		WMSetBoxHorizontal(row, True);
		panel->mouse_action[i].label = WMCreateLabel(row);
		WMSetLabelTextAlignment(panel->mouse_action[i].label, WARight);
		WMSetLabelText(panel->mouse_action[i].label, _(button_list[i].display_label));
		size = WMGetLabelPreferredSize(panel->mouse_action[i].label);
		size.width = WMAX(size.width, labelWidth);
		WMPackBoxSubview(row, panel->mouse_action[i].label, size, False, 8);

		panel->mouse_action[i].popup = WMCreatePopUpButton(row);

		if (button_list[i].type == T_BUTTON) {
			for (j = 0; j < wlengthof(button_actions); j++)
				WMAddPopUpButtonItem(panel->mouse_action[i].popup, _(button_actions[j].label));
		} else {
			for (j = 0; j < wlengthof(wheel_actions); j++)
				WMAddPopUpButtonItem(panel->mouse_action[i].popup, _(wheel_actions[j].label));
		}
		WMPackBoxSubview(row, panel->mouse_action[i].popup,
				 WMGetPopUpButtonPreferredSize(panel->mouse_action[i].popup), True, 0);
		WMPackBoxSubview(actions.content, row, WMGetBoxMinimumSize(row), False,
				 i == wlengthof(button_list) - 1 ? 0 : 8);
		WMMapSubwidgets(row);
	}
	row = WMCreateBox(actions.content);
	WMPackBoxSubview(actions.content, row, wmksize(1, 1), True, 0);
	WPrefsPackGroup(right, &actions, True, 0);
	WMPackBoxSubview(columns, left, WMGetBoxMinimumSize(left), True, 12);
	WMPackBoxSubview(columns, right, WMGetBoxMinimumSize(right), True, 0);
	WMPackBoxSubview(panel->box, columns, WMGetBoxMinimumSize(columns), True, 0);
	WMMapSubwidgets(left);
	WMMapSubwidgets(right);
	WMMapSubwidgets(columns);
	WMReleaseColor(textColor);
	WMReleaseFont(font);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

static void storeCommandInScript(const char *cmd, const char *line)
{
	char *path;
	FILE *f;
	char buffer[128];
	mode_t permissions;

	/* Calculate permission to be Executable but taking into account user's umask */
	permissions = umask(0);
	umask(permissions);
	permissions = (S_IRWXU | S_IRWXG | S_IRWXO) & (~permissions);

	path = wstrconcat(wuserdatapath(), "/" PACKAGE_TARNAME "/autostart");

	f = fopen(path, "rb");
	if (!f) {
		f = fopen(path, "wb");
		if (!f) {
			werror(_("could not create %s"), path);
			goto end;
		}
		fprintf(f, "#!/bin/sh\n");
		fputs(line, f);
		fputs("\n", f);
	} else {
		int len = strlen(cmd);
		int ok = 0;
		char *tmppath;
		FILE *fo;

		tmppath = wstrconcat(wuserdatapath(), "/" PACKAGE_TARNAME "/autostart.tmp");
		fo = fopen(tmppath, "wb");
		if (!fo) {
			werror(_("could not create temporary file %s"), tmppath);
			wfree(tmppath);
			goto end;
		}

		while (!feof(f)) {
			if (!fgets(buffer, 127, f)) {
				break;
			}
			if (buffer[0] == '\n') {
				/* don't write empty lines, else the file will grow
				 * indefinitely (one '\n' added at end of file on each save).
				 */
				continue;
			}
			if (strncmp(buffer, cmd, len) == 0) {
				if (!ok) {
					fputs(line, fo);
					fputs("\n", fo);
					ok = 1;
				}
			} else {
				fputs(buffer, fo);
			}
		}
		if (!ok) {
			fputs(line, fo);
			fputs("\n", fo);
		}
		fsync(fileno(fo));
		fclose(fo);

		if (rename(tmppath, path) != 0) {
			werror(_("could not rename file %s to %s"), tmppath, path);
		}
		wfree(tmppath);
	}
	if (chmod(path, permissions) != 0)
		wwarning(_("could not set permission 0%03o on file \"%s\""), permissions, path);

 end:
	wfree(path);
	if (f) {
		fsync(fileno(f));	/* this may be rw */
		fclose(f);
	}
}

static void storeData(_Panel * panel)
{
	char buffer[64];
	int i;
	char *tmp, *p;
	WMUserDefaults *udb = WMGetStandardUserDefaults();

	if (!WMGetUDBoolForKey(udb, "NoXSetStuff")) {
		tmp = WMGetTextFieldText(panel->threT);
		if (strlen(tmp) == 0) {
			wfree(tmp);
			tmp = wstrdup("4");
		}

		sprintf(buffer, XSET " m %i/%i %s\n", (int)(panel->acceleration * 10), 10, tmp);
		storeCommandInScript(XSET " m", buffer);

		wfree(tmp);
	}

	tmp = WMGetTextFieldText(panel->ddelaT);
	if (sscanf(tmp, "%i", &i) == 1 && i > 0)
		SetIntegerForKey(i, "DoubleClickTime");
	wfree(tmp);

	SetBoolForKey(WMGetButtonSelected(panel->disaB), "DisableWSMouseActions");

	for (i = 0; i < wlengthof(button_list); i++) {
		const char *db_value;
		int action;

		action = WMGetPopUpButtonSelectedItem(panel->mouse_action[i].popup);
		if (action < 0)
			continue;
		if (button_list[i].type == T_BUTTON)
			db_value = button_actions[action].db_value;
		else
			db_value = wheel_actions[action].db_value;
		SetStringForKey(db_value, button_list[i].db_key);
	}

	tmp = WMGetPopUpButtonItem(panel->grabP, WMGetPopUpButtonSelectedItem(panel->grabP));
	tmp = wstrdup(tmp);
	p = strchr(tmp, ' ');
	if (p != NULL)
		*p = '\0';

	SetStringForKey(tmp, "ModifierKey");

	wfree(tmp);
}

Panel *InitMouseSettings(WMWidget *parent)
{
	_Panel *panel;

	modifierNames[0] = wstrdup(_("Shift"));
	modifierNames[1] = wstrdup(_("Lock"));
	modifierNames[2] = wstrdup(_("Control"));
	modifierNames[3] = wstrdup(_("Mod1"));
	modifierNames[4] = wstrdup(_("Mod2"));
	modifierNames[5] = wstrdup(_("Mod3"));
	modifierNames[6] = wstrdup(_("Mod4"));
	modifierNames[7] = wstrdup(_("Mod5"));

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Mouse Preferences");

	panel->description = _("Mouse speed/acceleration, double click delay,\n" "mouse button bindings etc.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.showPanel = showPanel;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
