#include <sys/stat.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include "wmmenugen.h"

#ifdef HAVE_GIO
#include <gio/gdesktopappinfo.h>
#include "../src/applications_menu.h"
#include "wmmenugen_cache.h"
#include "../src/desktop_icons.h"

static void watchIconPath(void *data, const char *path)
{
	wApplicationsCacheWatchPath(data, path);
}

static gboolean hasCategory(GDesktopAppInfo *app, const char *category)
{
	const char *p = g_desktop_app_info_get_categories(app);
	size_t length = strlen(category);

	while (p && *p) {
		if (strncmp(p, category, length) == 0 && (p[length] == ';' || p[length] == '\0'))
			return TRUE;
		p = strchr(p, ';');
		if (p)
			p++;
	}
	return FALSE;
}

static const char *mainCategory(GDesktopAppInfo *app)
{
	if (hasCategory(app, "TerminalEmulator"))
		return "TerminalEmulator";
	for (unsigned int i = 1; i <= WM_APPLICATION_MAIN_CATEGORY_COUNT; i++)
		if (hasCategory(app, wApplicationsCategories[i].id))
			return wApplicationsCategories[i].id;
	return "Other";
}

static gint compareApps(gconstpointer left, gconstpointer right)
{
	int result = g_utf8_collate(g_app_info_get_name(G_APP_INFO(left)), g_app_info_get_name(G_APP_INFO(right)));

	if (!result)
		result = g_strcmp0(g_app_info_get_id(G_APP_INFO(left)), g_app_info_get_id(G_APP_INFO(right)));
	return result;
}

static WMPropList *applicationEntry(GAppInfo *app, WMDesktopIcons *icons)
{
	WMPropList *entry = WMCreatePLArray(NULL);
	const char *icon = icons ? wDesktopIconsResolve(icons, g_app_info_get_icon(app)) : NULL;
	char *quoted = g_shell_quote(g_app_info_get_id(app));
	GString *command = g_string_new("wmmenugen --launch ");
	const char *values[6] = { g_app_info_get_name(app), "ICON", icon, "EXEC", NULL, NULL };

	/* Window Maker expands percent signs and backslashes before invoking the shell. */
	for (const char *p = quoted; *p; p++) {
		if (*p == '%' || *p == '\\')
			g_string_append_c(command, '\\');
		g_string_append_c(command, *p);
	}
	values[4] = command->str;
	for (unsigned int i = 0; i < 5; i++) {
		WMPropList *value;

		if ((i == 1 || i == 2) && !icon)
			continue;
		value = WMCreatePLString(values[i]);
		WMAddToPLArray(entry, value);
		WMReleasePropList(value);
	}
	g_free(quoted);
	g_string_free(command, TRUE);
	return entry;
}

static WMPropList *newSubmenu(const char *title)
{
	WMPropList *name = WMCreatePLString(title);
	WMPropList *result = WMCreatePLArray(name, NULL);

	WMReleasePropList(name);
	return result;
}

int wmDesktopMenuMain(int argc, char **argv)
{
	gboolean applications = FALSE, noIcons = FALSE, noCache = FALSE;
	char *category = NULL, *launch = NULL, *themeName = NULL;
	int iconSize = 24, status = 0;
	GError *error = NULL;
	GOptionEntry options[] = {
		{ "applications", 0, 0, G_OPTION_ARG_NONE, &applications, "Generate the XDG Applications menu", NULL },
		{ "category", 0, 0, G_OPTION_ARG_STRING, &category, "Include only this desktop-entry category", "CATEGORY" },
		{ "no-cache", 0, 0, G_OPTION_ARG_NONE, &noCache, "Generate without reading or writing the menu cache", NULL },
		{ "no-icons", 0, 0, G_OPTION_ARG_NONE, &noIcons, "Omit application icons", NULL },
		{ "icon-theme", 0, 0, G_OPTION_ARG_STRING, &themeName, "Select an icon theme (default: GTK setting or hicolor)", "THEME" },
		{ "icon-size", 0, 0, G_OPTION_ARG_INT, &iconSize, "Preferred icon size in pixels (default: 24)", "SIZE" },
		{ "launch", 0, 0, G_OPTION_ARG_STRING, &launch, "Launch a desktop file ID or absolute filename", "DESKTOP-ID" },
		{ NULL }
	};
	GOptionContext *context = g_option_context_new(NULL);

	g_option_context_add_main_entries(context, options, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		fprintf(stderr, "%s\n", error->message);
		g_clear_error(&error);
		status = 2;
		goto finish;
	}
	if (argc != 1 || applications == (launch != NULL) ||
	    (category && !wApplicationsCategoryValid(category)) ||
	    (themeName && !wDesktopIconThemeValid(themeName)) || iconSize < 1 || iconSize > 512 ||
	    (launch && (category || themeName || noIcons || noCache || iconSize != 24))) {
		fprintf(stderr, "%s: invalid Applications menu arguments\n", argv[0]);
		status = 2;
		goto finish;
	}
	if (launch) {
		GDesktopAppInfo *app = g_path_is_absolute(launch) ?
			g_desktop_app_info_new_from_filename(launch) : g_desktop_app_info_new(launch);

		if (!app) {
			fprintf(stderr, "%s: could not load desktop entry \"%s\"\n", argv[0], launch);
			status = 1;
		} else {
			if (!g_app_info_launch(G_APP_INFO(app), NULL, NULL, &error)) {
				fprintf(stderr, "%s: could not launch \"%s\": %s\n", argv[0], launch, error->message);
				g_clear_error(&error);
				status = 1;
			}
			g_object_unref(app);
		}
	} else {
		GList *apps;
		WMDesktopIcons *icons;
		WMPropList *result;
		GHashTable *submenus;
		WMApplicationsCache *cache;
		char *description;

		if (!g_getenv("XDG_CURRENT_DESKTOP"))
			g_setenv("XDG_CURRENT_DESKTOP", "WindowMaker", FALSE);
		if (!themeName)
			themeName = wDesktopIconTheme();
		cache = noCache ? NULL : wApplicationsCacheCreate(argv[0], category, themeName, iconSize, noIcons);
		description = wApplicationsCacheLoad(cache);
		if (description) {
			if (puts(description) == EOF)
				status = 1;
			g_free(description);
			wApplicationsCacheDestroy(cache);
			goto finish;
		}
		wApplicationsCachePrepare(cache);
		icons = wDesktopIconsCreate(themeName, iconSize, watchIconPath, cache);
		submenus = g_hash_table_new(g_str_hash, g_str_equal);
		result = newSubmenu(category ? _(wApplicationsCategoryTitle(category)) : _("Applications"));
		apps = g_list_sort(g_app_info_get_all(), compareApps);
		for (GList *node = apps; node; node = node->next) {
			GAppInfo *app = node->data;
			GDesktopAppInfo *desktop;
			WMPropList *parent = result, *entry;

			if (!G_IS_DESKTOP_APP_INFO(app) || !g_app_info_get_id(app) || !g_app_info_should_show(app))
				continue;
			desktop = G_DESKTOP_APP_INFO(app);
			if (category && !(strcmp(category, "Other") == 0 ?
					 strcmp(mainCategory(desktop), "Other") == 0 : hasCategory(desktop, category)))
				continue;
			if (!category) {
				const char *id = mainCategory(desktop);

				parent = g_hash_table_lookup(submenus, id);
				if (!parent) {
					parent = newSubmenu(_(wApplicationsCategoryTitle(id)));
					g_hash_table_insert(submenus, (gpointer)id, parent);
				}
			}
			entry = applicationEntry(app, noIcons ? NULL : icons);
			WMAddToPLArray(parent, entry);
			WMReleasePropList(entry);
		}
		for (unsigned int i = 1; !category && i < WM_APPLICATION_CATEGORY_COUNT; i++) {
			WMPropList *submenu = g_hash_table_lookup(submenus, wApplicationsCategories[i].id);

			if (submenu) {
				WMAddToPLArray(result, submenu);
				WMReleasePropList(submenu);
			}
		}
		description = WMGetPropListDescription(result, TRUE);
		wApplicationsCacheSave(cache, description);
		if (puts(description) == EOF)
			status = 1;
		wfree(description);
		WMReleasePropList(result);
		g_hash_table_unref(submenus);
		g_list_free_full(apps, g_object_unref);
		wDesktopIconsDestroy(icons);
		wApplicationsCacheDestroy(cache);
	}
finish:
	g_option_context_free(context);
	g_free(category);
	g_free(launch);
	g_free(themeName);
	return status;
}
#else
int wmDesktopMenuMain(int argc, char **argv)
{
	(void)argc;
	fprintf(stderr, "%s: Applications menus were built without GIO support\n", argv[0]);
	return 2;
}
#endif
