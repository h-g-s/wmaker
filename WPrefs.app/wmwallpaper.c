/* wmwallpaper.c - standalone desktop wallpaper chooser
 *
 *  Window Maker window manager
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
#include "WallpaperChooser.h"

#include <X11/Xlocale.h>

static void print_help(const char *progname)
{
	printf(_("usage: %s [options] [folder]\n"), progname);
	puts(_("Choose the desktop wallpaper from the images in a folder."));
	puts("");
	puts(_(" -display <display>      display to be used"));
	puts(_(" -h, --help              print this message and exit"));
	puts(_(" -v, --version           print version number and exit"));
}

static void closeWindow(WMWidget *w, void *data)
{
	(void)w;
	(void)data;
	exit(0);
}

int main(int argc, char **argv)
{
	const char *displayName = "", *folder = NULL;
	WallpaperChooser *chooser;
	WMWindow *window;
	WMBox *column, *actions, *spacer;
	WMButton *closeB;
	WMScreen *screen;
	Display *dpy;
	WMSize size;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
			printf("wmwallpaper (Window Maker) %s\n", VERSION);
			return 0;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_help(argv[0]);
			return 0;
		} else if (strcmp(argv[i], "-display") == 0) {
			if (++i >= argc) {
				wwarning(_("too few arguments for %s"), argv[i - 1]);
				return 1;
			}
			displayName = argv[i];
		} else if (argv[i][0] == '-' || folder) {
			print_help(argv[0]);
			return 1;
		} else {
			folder = argv[i];
		}
	}

	setlocale(LC_ALL, "");
#ifdef I18N
	if (getenv("NLSPATH"))
		bindtextdomain("WPrefs", getenv("NLSPATH"));
	else
		bindtextdomain("WPrefs", LOCALEDIR);
	bind_textdomain_codeset("WPrefs", "UTF-8");
	textdomain("WPrefs");
#endif

	WMInitializeApplication("wmwallpaper", &argc, argv);
	dpy = XOpenDisplay(displayName);
	if (!dpy) {
		wfatal(_("could not open display %s"), XDisplayName(displayName));
		return 1;
	}
	screen = WMCreateScreen(dpy, DefaultScreen(dpy));
	if (!screen) {
		wfatal(_("could not initialize application"));
		return 1;
	}
	WMPLSetCaseSensitive(False);

	window = WMCreateWindow(screen, "wmwallpaper");
	WMSetWindowTitle(window, _("Wallpaper"));
	WMSetWindowCloseAction(window, closeWindow, NULL);

	column = WMCreateBox(window);
	WMSetBoxBorderWidth(column, 12);
	chooser = WallpaperChooserCreate(column);
	WMPackBoxSubview(column, WallpaperChooserGetBox(chooser), WallpaperChooserGetMinimumSize(chooser), True,
			 12);
	actions = WMCreateBox(column);
	WMSetBoxHorizontal(actions, True);
	spacer = WMCreateBox(actions);
	WMPackBoxSubview(actions, spacer, wmksize(1, 1), True, 0);
	closeB = WMCreateCommandButton(actions);
	WMSetButtonText(closeB, _("Close"));
	WMSetButtonAction(closeB, closeWindow, NULL);
	WPrefsPackButton(actions, closeB, 0);
	WMPackBoxSubview(column, actions, WMGetBoxMinimumSize(actions), False, 0);

	size = WMGetBoxMinimumSize(column);
	size.width = WMIN(size.width, WMScreenWidth(screen) - 40);
	size.height = WMIN(size.height, WMScreenHeight(screen) - 80);
	WMResizeWidget(window, size.width, size.height);
	WMSetWindowMinSize(window, size.width, size.height);
	WMResizeWidget(column, size.width, size.height);
	WMSetViewExpandsToParent(WMWidgetView(column), 0, 0, 0, 0);

	WMRealizeWidget(window);
	WMMapSubwidgets(actions);
	WMMapSubwidgets(WallpaperChooserGetBox(chooser));
	WMMapSubwidgets(column);
	WMMapSubwidgets(window);
	WMMapWidget(window);

	if (folder)
		WallpaperChooserSetFolder(chooser, folder);
	else
		WallpaperChooserStart(chooser);
	WMScreenMainLoop(screen);
}
