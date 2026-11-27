/* wmapplications - WINGs default application chooser
 * Copyright (C) 2026 Window Maker contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "WPrefs.h"
#include "PanelLayout.h"
#include "ApplicationDefaults.h"
#include "../src/desktop_icons.h"
#include <X11/Xlocale.h>

static void print_help(const char *program)
{
	printf(_("usage: %s [options]\n"), program);
	puts(_("Choose default desktop applications."));
	puts("");
	puts(_(" -display <display>      display to be used"));
	puts(_(" -h, --help              print this message and exit"));
	puts(_(" -v, --version           print version number and exit"));
}

#ifdef HAVE_GIO
typedef struct {
	WMDefaultRole role;
	WMPopUpButton *popup;
	WMLabel *icon;
	GPtrArray *apps;
	int saved;
	int shown;
} ApplicationRow;

typedef struct {
	WMWindow *window;
	WMButton *apply;
	WMDesktopIcons *icons;
	GHashTable *pixmaps;
	ApplicationRow rows[WMDefaultRoleCount];
} ApplicationChooser;

static const char *const roleNames[] = {
	N_("Web browser"), N_("Email"), N_("Terminal"), N_("File manager"), N_("Archive manager")
};

static void closeWindow(WMWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	exit(0);
}

static void releaseIcon(gpointer data)
{
	if (data)
		WMReleasePixmap(data);
}

static WMPixmap *loadIcon(WMScreen *screen, const char *path)
{
	RImage *image = RLoadImage(WMScreenRContext(screen), path, 0);
	WMPixmap *pixmap;

	if (!image) {
		wwarning(_("could not load icon %s: %s"), path, RMessageForError(RErrorCode));
		return NULL;
	}
	if (image->width > 24 || image->height > 24) {
		int width = 24, height = 24;
		RImage *scaled;

		if (image->width >= image->height)
			height = WMAX(1, (int)(24LL * image->height / image->width));
		else
			width = WMAX(1, (int)(24LL * image->width / image->height));
		scaled = RScaleImage(image, width, height);
		RReleaseImage(image);
		image = scaled;
		if (!image) {
			wwarning(_("could not scale icon %s: %s"), path, RMessageForError(RErrorCode));
			return NULL;
		}
	}
	pixmap = WMCreatePixmapFromRImage(screen, image, 127);
	RReleaseImage(image);
	if (!pixmap)
		wwarning(_("could not process icon %s: %s"), path, RMessageForError(RErrorCode));
	return pixmap;
}

static void updateRowIcon(ApplicationChooser *chooser, ApplicationRow *row)
{
	int selected = WMGetPopUpButtonSelectedItem(row->popup);
	WMPixmap *pixmap = NULL;
	const char *path = NULL;

	if (selected == row->shown)
		return;
	row->shown = selected;
	if (selected > 0) {
		GAppInfo *app = g_ptr_array_index(row->apps, selected - 1);

		path = wDesktopIconsResolve(chooser->icons, g_app_info_get_icon(app));
	}
	if (path) {
		if (!g_hash_table_contains(chooser->pixmaps, path)) {
			pixmap = loadIcon(WMWidgetScreen(row->icon), path);
			g_hash_table_insert(chooser->pixmaps, g_strdup(path), pixmap);
		} else {
			pixmap = g_hash_table_lookup(chooser->pixmaps, path);
		}
	}
	WMSetLabelImage(row->icon, pixmap);
}

static void selectionChanged(WMWidget *widget, void *data)
{
	ApplicationChooser *chooser = data;
	Bool changed = False;

	(void)widget;
	for (int i = 0; i < WMDefaultRoleCount; i++) {
		int selected = WMGetPopUpButtonSelectedItem(chooser->rows[i].popup);

		updateRowIcon(chooser, &chooser->rows[i]);
		if (selected > 0 && selected != chooser->rows[i].saved)
			changed = True;
	}
	WMSetButtonEnabled(chooser->apply, changed);
}

static void applyDefaults(WMWidget *widget, void *data)
{
	ApplicationChooser *chooser = data;

	(void)widget;
	for (int i = 0; i < WMDefaultRoleCount; i++) {
		ApplicationRow *row = &chooser->rows[i];
		int selected = WMGetPopUpButtonSelectedItem(row->popup);
		GError *error = NULL;

		if (selected <= 0 || selected == row->saved)
			continue;
		if (!WMSetDefaultApplication(row->role, g_ptr_array_index(row->apps, selected - 1), &error)) {
			char *message = g_strdup_printf(_("%s: %s\n\nSome associations may already have changed."),
						       _(roleNames[i]), error->message);

			wwarning("%s", message);
			WMRunAlertPanel(WMWidgetScreen(chooser->window), chooser->window,
					_("Could not save defaults"), message, _("OK"), NULL, NULL);
			g_free(message);
			g_clear_error(&error);
			selectionChanged(NULL, chooser);
			return;
		}
		row->saved = selected;
	}
	selectionChanged(NULL, chooser);
}

static void createRow(ApplicationChooser *chooser, WMBox *column, WMDefaultRole role)
{
	ApplicationRow *row = &chooser->rows[role];
	WMBox *box = WMCreateBox(column);
	WMLabel *label = WMCreateLabel(box);
	GError *error = NULL;
	char *current, *terminalHelper = NULL;
	WMSize labelSize, popupSize;

	if (!chooser->icons) {
		chooser->icons = wDesktopIconsCreate(NULL, 24, NULL, NULL);
		chooser->pixmaps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, releaseIcon);
	}
	row->role = role;
	row->shown = -1;
	row->apps = WMDefaultApplications(role);
	WMSetBoxHorizontal(box, True);
	WMSetLabelText(label, _(roleNames[role]));
	labelSize = WMGetLabelPreferredSize(label);
	labelSize.width = WMAX(130, labelSize.width);
	row->icon = WMCreateLabel(box);
	WMSetLabelImagePosition(row->icon, WIPImageOnly);
	row->popup = WMCreatePopUpButton(box);
	WMAddPopUpButtonItem(row->popup, _("Keep existing defaults"));
	for (unsigned int i = 0; i < row->apps->len; i++) {
		GAppInfo *app = g_ptr_array_index(row->apps, i);
		WMAddPopUpButtonItem(row->popup, g_app_info_get_display_name(app));
	}
	if (role == WMDefaultTerminal)
		terminalHelper = g_find_program_in_path("xdg-terminal-exec");
	current = role != WMDefaultTerminal || terminalHelper ? WMDefaultApplicationID(role, &error) : NULL;
	if (error) {
		wwarning(_("could not read default application: %s"), error->message);
		g_clear_error(&error);
	}
	for (unsigned int i = 0; current && i < row->apps->len; i++)
		if (g_strcmp0(current, g_app_info_get_id(g_ptr_array_index(row->apps, i))) == 0)
			row->saved = i + 1;
	WMSetPopUpButtonSelectedItem(row->popup, row->saved);
	updateRowIcon(chooser, row);
	WMSetPopUpButtonEnabled(row->popup, row->apps->len > 0 &&
			       (role != WMDefaultTerminal || terminalHelper != NULL));
	WMSetPopUpButtonAction(row->popup, selectionChanged, chooser);
	popupSize = WMGetPopUpButtonPreferredSize(row->popup);
	popupSize.width = 420;
	labelSize.height = popupSize.height = WMAX(24, WMAX(labelSize.height, popupSize.height));
	WMPackBoxSubview(box, label, labelSize, False, 8);
	WMPackBoxSubview(box, row->icon, wmksize(24, popupSize.height), False, 8);
	WMPackBoxSubview(box, row->popup, popupSize, True, 0);
	WMPackBoxSubview(column, box, WMGetBoxMinimumSize(box), False, 12);
	WMMapSubwidgets(box);
	g_free(current);
	g_free(terminalHelper);
}
#endif

int main(int argc, char **argv)
{
	const char *displayName = "";

	setlocale(LC_ALL, "");
#ifdef I18N
	bindtextdomain("WPrefs", getenv("NLSPATH") ? getenv("NLSPATH") : LOCALEDIR);
	bind_textdomain_codeset("WPrefs", "UTF-8");
	textdomain("WPrefs");
#endif
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_help(argv[0]);
			return 0;
		} else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
			printf("wmapplications (Window Maker) %s\n", VERSION);
			return 0;
		} else if (strcmp(argv[i], "-display") == 0) {
			if (++i >= argc) {
				wwarning(_("too few arguments for %s"), argv[i - 1]);
				return 1;
			}
			displayName = argv[i];
		} else {
			wwarning(_("unknown option: %s"), argv[i]);
			print_help(argv[0]);
			return 1;
		}
	}
#ifdef HAVE_GIO
	ApplicationChooser chooser = { 0 };
	WMBox *column, *actions, *spacer;
	WMLabel *note;
	WMButton *close;
	WMSize size;
	Display *display;
	WMScreen *screen;

	WMInitializeApplication("wmapplications", &argc, argv);
	display = XOpenDisplay(displayName);
	if (!display) {
		wfatal(_("could not open display %s"), XDisplayName(displayName));
		return 1;
	}
	screen = WMCreateScreen(display, DefaultScreen(display));
	if (!screen) {
		wfatal(_("could not initialize application"));
		return 1;
	}
	chooser.window = WMCreateWindow(screen, "wmapplications");
	WMSetWindowTitle(chooser.window, _("Default Applications"));
	WMSetWindowCloseAction(chooser.window, closeWindow, NULL);
	column = WMCreateBox(chooser.window);
	WMSetBoxBorderWidth(column, 12);
	for (int i = 0; i < WMDefaultRoleCount; i++)
		createRow(&chooser, column, i);
	note = WMCreateLabel(column);
	WMSetLabelWraps(note, True);
	WMSetLabelText(note, _("Only supported MIME/URL types are changed; other defaults are kept.\n"
			      "Terminal requires xdg-terminal-exec and a compatible desktop entry.\n"
			      "It does not change legacy terminal commands used by other programs."));
	WMPackBoxSubview(column, note, WMGetLabelPreferredSizeForWidth(note, 558), False, 12);
	actions = WMCreateBox(column);
	WMSetBoxHorizontal(actions, True);
	spacer = WMCreateBox(actions);
	WMPackBoxSubview(actions, spacer, wmksize(1, 1), True, 0);
	chooser.apply = WMCreateCommandButton(actions);
	WMSetButtonText(chooser.apply, _("Apply"));
	WMSetButtonAction(chooser.apply, applyDefaults, &chooser);
	WMSetButtonEnabled(chooser.apply, False);
	WPrefsPackButton(actions, chooser.apply, 12);
	close = WMCreateCommandButton(actions);
	WMSetButtonText(close, _("Close"));
	WMSetButtonAction(close, closeWindow, NULL);
	WPrefsPackButton(actions, close, 0);
	WMPackBoxSubview(column, actions, WMGetBoxMinimumSize(actions), False, 0);
	size = WMGetBoxMinimumSize(column);
	WMResizeWidget(chooser.window, size.width, size.height);
	WMSetWindowMinSize(chooser.window, size.width, size.height);
	WMResizeWidget(column, size.width, size.height);
	WMSetViewExpandsToParent(WMWidgetView(column), 0, 0, 0, 0);
	WMSetWindowInitialPosition(chooser.window, WMAX(0, ((int)WMScreenWidth(screen) - (int)size.width) / 2),
				  WMAX(0, ((int)WMScreenHeight(screen) - (int)size.height) / 2));
	WMRealizeWidget(chooser.window);
	WMMapSubwidgets(actions);
	WMMapSubwidgets(column);
	WMMapSubwidgets(chooser.window);
	WMMapWidget(chooser.window);
	WMScreenMainLoop(screen);
#else
	(void)displayName;
	wwarning(_("wmapplications requires a build with GIO desktop application support"));
	return 1;
#endif
}
