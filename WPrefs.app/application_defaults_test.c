/* Isolated desktop association tests; never use the session's XDG directories.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ApplicationDefaults.h"

#ifdef HAVE_GIO
#include <glib/gstdio.h>
#include <string.h>

#define main wmapplications_main
#include "wmapplications.c"
#undef main

static char *root, *configDir, *dataDir, *applicationsDir;

static void writeFile(const char *directory, const char *name, const char *contents)
{
	char *path = g_build_filename(directory, name, NULL);
	GError *error = NULL;

	g_assert_true(g_file_set_contents(path, contents, -1, &error));
	g_assert_no_error(error);
	g_free(path);
}

static GAppInfo *getApp(const char *id)
{
	GDesktopAppInfo *app = g_desktop_app_info_new(id);

	g_assert_nonnull(app);
	return G_APP_INFO(app);
}

static void assertDefault(const char *type, const char *id)
{
	GAppInfo *app = g_app_info_get_default_for_type(type, FALSE);

	g_assert_nonnull(app);
	g_assert_cmpstr(g_app_info_get_id(app), ==, id);
	g_object_unref(app);
}

static gboolean containsApp(GPtrArray *apps, const char *id)
{
	for (unsigned int i = 0; i < apps->len; i++)
		if (g_strcmp0(g_app_info_get_id(g_ptr_array_index(apps, i)), id) == 0)
			return TRUE;
	return FALSE;
}

static void discoveryTest(void)
{
	GPtrArray *apps = WMDefaultApplications(WMDefaultBrowser);

	g_assert_true(containsApp(apps, "browser.desktop"));
	g_assert_false(containsApp(apps, "browser-alias.desktop"));
	g_assert_false(containsApp(apps, "hidden.desktop"));
	g_assert_false(containsApp(apps, "archive.desktop"));
	g_assert_false(containsApp(apps, "missing.desktop"));
	g_assert_cmpuint(apps->len, ==, 2);
	g_ptr_array_unref(apps);
	apps = WMDefaultApplications(WMDefaultTerminal);
	g_assert_true(containsApp(apps, "terminal.desktop"));
	g_assert_false(containsApp(apps, "legacy-terminal.desktop"));
	g_assert_cmpuint(apps->len, ==, 1);
	g_ptr_array_unref(apps);
}

static void noDisplayDefaultTest(void)
{
	GAppInfo *alias = getApp("browser-alias.desktop");
	GAppInfo *browser = getApp("browser.desktop");
	GError *error = NULL;
	char *id;

	g_assert_true(g_desktop_app_info_get_nodisplay(G_DESKTOP_APP_INFO(alias)));
	g_assert_true(WMSetDefaultApplication(WMDefaultBrowser, alias, &error));
	g_assert_no_error(error);
	id = WMDefaultApplicationID(WMDefaultBrowser, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(id, ==, "browser-alias.desktop");
	g_free(id);
	g_assert_true(WMSetDefaultApplication(WMDefaultBrowser, browser, &error));
	g_assert_no_error(error);
	g_object_unref(alias);
	g_object_unref(browser);
}

static void associationsTest(void)
{
	GError *error = NULL;
	GAppInfo *old = getApp("old.desktop");
	GAppInfo *browser = getApp("browser.desktop");
	GAppInfo *archive = getApp("archive.desktop");
	char *id;

	g_assert_true(g_app_info_set_as_default_for_type(old, "application/x-tar", &error));
	g_assert_true(g_app_info_set_as_default_for_type(old, "text/plain", &error));
	g_assert_true(g_app_info_set_as_default_for_type(old, "x-scheme-handler/https", &error));
	g_assert_true(g_app_info_set_as_default_for_type(browser, "x-scheme-handler/http", &error));
	g_assert_no_error(error);
	id = WMDefaultApplicationID(WMDefaultBrowser, &error);
	g_assert_null(id);
	g_assert_true(WMSetDefaultApplication(WMDefaultBrowser, browser, &error));
	g_assert_no_error(error);
	assertDefault("x-scheme-handler/http", "browser.desktop");
	assertDefault("x-scheme-handler/https", "browser.desktop");
	assertDefault("text/html", "browser.desktop");
	id = WMDefaultApplicationID(WMDefaultBrowser, &error);
	g_assert_cmpstr(id, ==, "browser.desktop");
	g_free(id);
	g_assert_true(WMSetDefaultApplication(WMDefaultArchiveManager, archive, &error));
	g_assert_no_error(error);
	assertDefault("application/zip", "archive.desktop");
	assertDefault("application/x-tar", "old.desktop");
	assertDefault("text/plain", "old.desktop");
	g_assert_false(WMSetDefaultApplication(WMDefaultEmail, archive, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error(&error);
	g_object_unref(old);
	g_object_unref(browser);
	g_object_unref(archive);
}

static void otherRolesTest(void)
{
	GError *error = NULL;
	GAppInfo *app = getApp("files-mail.desktop");

	g_assert_true(WMSetDefaultApplication(WMDefaultEmail, app, &error));
	g_assert_true(WMSetDefaultApplication(WMDefaultFileManager, app, &error));
	g_assert_no_error(error);
	assertDefault("x-scheme-handler/mailto", "files-mail.desktop");
	assertDefault("inode/directory", "files-mail.desktop");
	g_object_unref(app);
}

static void terminalTest(void)
{
	GError *error = NULL;
	GAppInfo *app = getApp("terminal.desktop");
	const char *previous = "# Keep this comment\n/execarg_strict\n-old.desktop\nold.desktop\n";
	char *path = g_build_filename(configDir, "windowmaker-xdg-terminals.list", NULL);
	char *contents = NULL;

	g_setenv("XDG_CURRENT_DESKTOP", "WindowMaker:Other", TRUE);
	writeFile(configDir, "xdg-terminals.list", "generic.desktop\n");
	writeFile(configDir, "windowmaker-xdg-terminals.list", previous);
	g_assert_true(WMSetDefaultApplication(WMDefaultTerminal, app, &error));
	g_assert_no_error(error);
	g_assert_true(g_file_get_contents(path, &contents, NULL, &error));
	g_assert_cmpstr(contents, ==,
			"terminal.desktop\n# Keep this comment\n/execarg_strict\n-old.desktop\nold.desktop\n");
	g_free(contents);
	g_free(path);
	g_unsetenv("XDG_CURRENT_DESKTOP");
	g_assert_true(WMSetDefaultApplication(WMDefaultTerminal, app, &error));
	path = g_build_filename(configDir, "xdg-terminals.list", NULL);
	g_assert_true(g_file_get_contents(path, &contents, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(contents, ==, "terminal.desktop\ngeneric.desktop\n");
	g_free(contents);
	g_free(path);
	g_object_unref(app);
}

static void terminalErrorTest(void)
{
	GError *error = NULL;
	GAppInfo *app = getApp("terminal.desktop");
	char *path = g_build_filename(configDir, "broken-xdg-terminals.list", NULL);

	g_setenv("XDG_CURRENT_DESKTOP", "Broken", TRUE);
	g_assert_cmpint(g_mkdir(path, 0700), ==, 0);
	g_assert_false(WMSetDefaultApplication(WMDefaultTerminal, app, &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_cmpint(g_rmdir(path), ==, 0);
	g_free(path);
	g_object_unref(app);
}

static void terminalCurrentTest(void)
{
	GError *error = NULL;
	char *originalPath = g_strdup(g_getenv("PATH"));
	char *searchPath = g_strconcat(root, ":", originalPath ? originalPath : "", NULL);
	char *helper = g_build_filename(root, "xdg-terminal-exec", NULL);
	char *id;

	writeFile(root, "xdg-terminal-exec",
		  "#!/bin/sh\n[ \"$1\" = --print-id ] || exit 2\nprintf 'terminal.desktop\\n'\n");
	g_assert_cmpint(g_chmod(helper, 0700), ==, 0);
	g_setenv("PATH", searchPath, TRUE);
	id = WMDefaultApplicationID(WMDefaultTerminal, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(id, ==, "terminal.desktop");
	g_free(id);
	writeFile(root, "xdg-terminal-exec", "#!/bin/sh\necho 'No usable terminal' >&2\nexit 1\n");
	g_assert_cmpint(g_chmod(helper, 0700), ==, 0);
	id = WMDefaultApplicationID(WMDefaultTerminal, &error);
	g_assert_null(id);
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, "No usable terminal"));
	g_clear_error(&error);
	g_assert_cmpint(g_remove(helper), ==, 0);
	if (originalPath)
		g_setenv("PATH", originalPath, TRUE);
	else
		g_unsetenv("PATH");
	g_free(originalPath);
	g_free(searchPath);
	g_free(helper);
}

static void iconThemeTest(void)
{
	WMDesktopIcons *icons = wDesktopIconsCreate(NULL, 24, NULL, NULL);
	GIcon *icon = g_themed_icon_new("browser-themed");
	char *expected = g_build_filename(dataDir, "icons", "TestParent", "48x48", "apps",
					 "browser-themed.xpm", NULL);

	g_assert_cmpstr(wDesktopIconsResolve(icons, icon), ==, expected);
	g_object_unref(icon);
	g_free(expected);
	icon = g_themed_icon_new("old-themed");
	expected = g_build_filename(dataDir, "icons", "hicolor", "48x48", "apps", "old-themed.xpm", NULL);
	g_assert_cmpstr(wDesktopIconsResolve(icons, icon), ==, expected);
	g_object_unref(icon);
	g_free(expected);
	icon = g_themed_icon_new("missing-test-icon");
	g_assert_null(wDesktopIconsResolve(icons, icon));
	g_object_unref(icon);
	wDesktopIconsDestroy(icons);
}

static WMPixmap *selectedIcon(ApplicationRow *row)
{
	return WMGetMenuItemPixmap(WMGetPopUpButtonMenuItem(row->popup,
							  WMGetPopUpButtonSelectedItem(row->popup)));
}

static void dialogTest(void)
{
	Display *display = XOpenDisplay(NULL);
	WMScreen *screen;
	ApplicationChooser chooser = { 0 };
	WMBox *column;
	char *path, *before, *after;
	GError *error = NULL;
	ApplicationRow *browser;
	WMPixmap *browserIcon, *oldIcon;
	WMSize size;
	int selected = 0;

	if (!display) {
		g_test_skip("No test X display");
		return;
	}
	screen = WMCreateScreen(display, DefaultScreen(display));
	g_assert_nonnull(screen);
	chooser.window = WMCreateWindow(screen, "applicationDefaultsTest");
	column = WMCreateBox(chooser.window);
	for (int i = 0; i < WMDefaultRoleCount; i++)
		createRow(&chooser, column, i);
	for (int i = 0; i < WMDefaultRoleCount; i++) {
		ApplicationRow *row = &chooser.rows[i];

		for (unsigned int j = 0; j < row->apps->len; j++)
			g_assert_cmpstr(WMGetPopUpButtonItem(row->popup, j + 1), ==,
					g_app_info_get_display_name(g_ptr_array_index(row->apps, j)));
	}
	chooser.apply = WMCreateCommandButton(column);
	selectionChanged(NULL, &chooser);
	g_assert_false(WMGetButtonEnabled(chooser.apply));
	path = g_build_filename(configDir, "mimeapps.list", NULL);
	g_assert_true(g_file_get_contents(path, &before, NULL, &error));
	applyDefaults(NULL, &chooser);
	g_assert_true(g_file_get_contents(path, &after, NULL, &error));
	g_assert_cmpstr(before, ==, after);
	g_free(before);
	g_free(after);
	browser = &chooser.rows[WMDefaultBrowser];
	browserIcon = selectedIcon(browser);
	g_assert_nonnull(browserIcon);
	size = WMGetPixmapSize(browserIcon);
	g_assert_cmpuint(size.width, ==, 24);
	g_assert_cmpuint(size.height, ==, 12);
	g_assert_nonnull(selectedIcon(&chooser.rows[WMDefaultFileManager]));
	g_assert_null(selectedIcon(&chooser.rows[WMDefaultArchiveManager]));
	for (unsigned int i = 0; i < browser->apps->len; i++)
		if (g_strcmp0(g_app_info_get_id(g_ptr_array_index(browser->apps, i)), "old.desktop") == 0)
			selected = i + 1;
	g_assert_cmpint(selected, >, 0);
	WMSetPopUpButtonSelectedItem(browser->popup, selected);
	selectionChanged(NULL, &chooser);
	g_assert_true(WMGetButtonEnabled(chooser.apply));
	oldIcon = selectedIcon(browser);
	g_assert_nonnull(oldIcon);
	g_assert_true(oldIcon != browserIcon);
	WMSetPopUpButtonSelectedItem(browser->popup, 0);
	selectionChanged(NULL, &chooser);
	g_assert_false(WMGetButtonEnabled(chooser.apply));
	g_assert_null(selectedIcon(browser));
	WMSetPopUpButtonSelectedItem(browser->popup, selected);
	selectionChanged(NULL, &chooser);
	g_assert_true(selectedIcon(browser) == oldIcon);
	applyDefaults(NULL, &chooser);
	g_assert_false(WMGetButtonEnabled(chooser.apply));
	assertDefault("x-scheme-handler/https", "old.desktop");
	assertDefault("x-scheme-handler/http", "browser.desktop");
	assertDefault("application/zip", "archive.desktop");
	g_assert_no_error(error);
	for (int i = 0; i < WMDefaultRoleCount; i++)
		g_ptr_array_unref(chooser.rows[i].apps);
	WMDestroyWidget(chooser.window);
	g_hash_table_unref(chooser.pixmaps);
	wDesktopIconsDestroy(chooser.icons);
	g_free(path);
}

static void removeFiles(const char *directory)
{
	GDir *dir = g_dir_open(directory, 0, NULL);
	const char *name;

	g_assert_nonnull(dir);
	while ((name = g_dir_read_name(dir))) {
		char *path = g_build_filename(directory, name, NULL);

		if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
			removeFiles(path);
			g_assert_cmpint(g_rmdir(path), ==, 0);
		} else {
			g_assert_cmpint(g_remove(path), ==, 0);
		}
		g_free(path);
	}
	g_dir_close(dir);
}

static void createTheme(const char *name, const char *inherits, const char *iconName)
{
	char *base = g_build_filename(dataDir, "icons", name, NULL);
	char *directory = g_build_filename(base, "48x48", "apps", NULL);
	char *index = g_strdup_printf("[Icon Theme]\nName=%s\nDirectories=48x48/apps\nInherits=%s\n"
				     "[48x48/apps]\nSize=48\nType=Fixed\n", name, inherits);

	g_assert_cmpint(g_mkdir_with_parents(directory, 0700), ==, 0);
	writeFile(base, "index.theme", index);
	if (iconName) {
		GString *xpm = g_string_new("/* XPM */\nstatic char *icon[] = {\n\"48 24 1 1\",\n\". c #cc3333\",\n");
		char *pixels = g_strnfill(48, '.');

		for (int i = 0; i < 24; i++)
			g_string_append_printf(xpm, "\"%s\"%s\n", pixels, i == 23 ? "};" : ",");
		writeFile(directory, iconName, xpm->str);
		g_free(pixels);
		g_string_free(xpm, TRUE);
	}
	g_free(index);
	g_free(directory);
	g_free(base);
}
#endif

int main(int argc, char **argv)
{
#ifdef HAVE_GIO
	GError *error = NULL;
	int result;

	g_test_init(&argc, &argv, NULL);
	root = g_dir_make_tmp("wmapplications-test-XXXXXX", &error);
	g_assert_no_error(error);
	configDir = g_build_filename(root, "config", NULL);
	dataDir = g_build_filename(root, "data", NULL);
	applicationsDir = g_build_filename(dataDir, "applications", NULL);
	g_assert_cmpint(g_mkdir(configDir, 0700), ==, 0);
	g_assert_cmpint(g_mkdir_with_parents(applicationsDir, 0700), ==, 0);
	g_setenv("HOME", root, TRUE);
	g_setenv("XDG_CONFIG_HOME", configDir, TRUE);
	g_setenv("XDG_DATA_HOME", dataDir, TRUE);
	g_setenv("XDG_CONFIG_DIRS", configDir, TRUE);
	g_setenv("XDG_DATA_DIRS", dataDir, TRUE);
	g_setenv("XDG_CACHE_HOME", root, TRUE);
	g_unsetenv("XDG_CURRENT_DESKTOP");
	writeFile(applicationsDir, "browser.desktop",
		  "[Desktop Entry]\nType=Application\nName=Browser\nExec=/bin/true %u\n"
		  "Icon=browser-themed\n"
		  "MimeType=x-scheme-handler/http;x-scheme-handler/https;text/html;\n");
	writeFile(applicationsDir, "browser-alias.desktop",
		  "[Desktop Entry]\nType=Application\nName=Browser\nExec=/bin/true %u\n"
		  "Icon=browser-themed\nNoDisplay=true\n"
		  "MimeType=x-scheme-handler/http;x-scheme-handler/https;text/html;\n");
	writeFile(applicationsDir, "old.desktop",
		  "[Desktop Entry]\nType=Application\nName=Old\nExec=/bin/true %u\n"
		  "Icon=old-themed\n"
		  "MimeType=x-scheme-handler/https;application/x-tar;text/plain;\n");
	writeFile(applicationsDir, "archive.desktop",
		  "[Desktop Entry]\nType=Application\nName=Archive\nExec=/bin/true %f\n"
		  "Icon=missing-test-icon\n"
		  "MimeType=application/zip;\n");
	{
		char *icon = g_build_filename(dataDir, "icons", "TestParent", "48x48", "apps",
					     "browser-themed.xpm", NULL);
		char *desktop = g_strdup_printf(
			"[Desktop Entry]\nType=Application\nName=Files and Mail\nExec=/bin/true %%u\n"
			"Icon=%s\nMimeType=inode/directory;x-scheme-handler/mailto;\n", icon);

		writeFile(applicationsDir, "files-mail.desktop", desktop);
		g_free(desktop);
		g_free(icon);
	}
	writeFile(applicationsDir, "hidden.desktop",
		  "[Desktop Entry]\nType=Application\nName=Hidden\nExec=/bin/true %u\nHidden=true\n"
		  "MimeType=x-scheme-handler/http;\n");
	writeFile(applicationsDir, "missing.desktop",
		  "[Desktop Entry]\nType=Application\nName=Missing\nExec=/bin/true\n"
		  "TryExec=/nonexistent/wmapplications-test\nMimeType=x-scheme-handler/http;\n");
	writeFile(applicationsDir, "terminal.desktop",
		  "[Desktop Entry]\nType=Application\nName=Terminal\nExec=/bin/true\n"
		  "Categories=System;TerminalEmulator;\nX-TerminalArgExec=\n");
	writeFile(applicationsDir, "legacy-terminal.desktop",
		  "[Desktop Entry]\nType=Application\nName=Legacy\nExec=/bin/true\n"
		  "Categories=TerminalEmulator;\n");
	writeFile(configDir, "mimeapps.list",
		  "[Default Applications]\n"
		  "x-scheme-handler/http=browser.desktop;\n"
		  "x-scheme-handler/https=browser.desktop;\n"
		  "text/html=browser.desktop;\n"
		  "application/zip=archive.desktop;\n"
		  "application/x-tar=old.desktop;\n"
		  "text/plain=old.desktop;\n"
		  "inode/directory=files-mail.desktop;\n"
		  "x-scheme-handler/mailto=files-mail.desktop;\n");
	{
		char *gtk = g_build_filename(configDir, "gtk-4.0", NULL);

		g_assert_cmpint(g_mkdir(gtk, 0700), ==, 0);
		writeFile(gtk, "settings.ini", "[Settings]\ngtk-icon-theme-name=TestSelected\n");
		g_free(gtk);
	}
	createTheme("TestSelected", "TestParent", NULL);
	createTheme("TestParent", "", "browser-themed.xpm");
	createTheme("hicolor", "", "old-themed.xpm");
	g_test_add_func("/applications/discovery", discoveryTest);
	g_test_add_func("/applications/nodisplay-default", noDisplayDefaultTest);
	g_test_add_func("/applications/associations", associationsTest);
	g_test_add_func("/applications/other-roles", otherRolesTest);
	g_test_add_func("/applications/terminal", terminalTest);
	g_test_add_func("/applications/terminal-error", terminalErrorTest);
	g_test_add_func("/applications/terminal-current", terminalCurrentTest);
	g_test_add_func("/applications/icon-theme", iconThemeTest);
	WMInitializeApplication("applicationDefaultsTest", &argc, argv);
	g_test_add_func("/applications/dialog", dialogTest);
	result = g_test_run();
	removeFiles(applicationsDir);
	removeFiles(configDir);
	g_assert_cmpint(g_rmdir(applicationsDir), ==, 0);
	removeFiles(dataDir);
	g_assert_cmpint(g_rmdir(dataDir), ==, 0);
	g_assert_cmpint(g_rmdir(configDir), ==, 0);
	removeFiles(root);
	g_assert_cmpint(g_rmdir(root), ==, 0);
	g_free(applicationsDir);
	g_free(dataDir);
	g_free(configDir);
	g_free(root);
	return result;
#else
	(void)argc;
	(void)argv;
	return 77;
#endif
}
