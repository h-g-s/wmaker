#include <sys/stat.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include "wmmenugen.h"

#ifdef HAVE_GIO
#include <gio/gdesktopappinfo.h>
#include "../src/applications_menu.h"
#include "wmmenugen_cache.h"

typedef struct {
	char *path;
	int distance;
	GHashTable *files;
} IconDirectory;

typedef struct {
	GPtrArray *directories;
	char **inherits;
} IconTheme;

typedef struct {
	GPtrArray *bases;
	GPtrArray *fallbacks;
	GHashTable *themes;
	GHashTable *icons;
	char *name;
	int size;
	WMApplicationsCache *cache;
} IconLookup;

static void freeIconDirectory(gpointer data)
{
	IconDirectory *directory = data;

	g_free(directory->path);
	if (directory->files)
		g_hash_table_unref(directory->files);
	g_free(directory);
}

static void freeIconTheme(gpointer data)
{
	IconTheme *theme = data;

	g_ptr_array_unref(theme->directories);
	g_strfreev(theme->inherits);
	g_free(theme);
}

static gboolean relativePath(const char *path)
{
	char **parts;
	gboolean valid = TRUE;

	if (!path || !*path || g_path_is_absolute(path))
		return FALSE;
	parts = g_strsplit(path, "/", -1);
	for (unsigned int i = 0; parts[i]; i++)
		if (!*parts[i] || strcmp(parts[i], "..") == 0)
			valid = FALSE;
	g_strfreev(parts);
	return valid;
}

static gboolean validThemeName(const char *name)
{
	if (!relativePath(name) || strchr(name, '/') || strcmp(name, ".") == 0)
		return FALSE;
	for (const unsigned char *p = (const unsigned char *)name; *p; p++)
		if (*p <= ' ' || *p >= 127 || *p == ',')
			return FALSE;
	return TRUE;
}

static void addThemeDirectories(IconLookup *lookup, IconTheme *theme, GKeyFile *index,
				const char *name, const char *key)
{
	char **directories = g_key_file_get_string_list(index, "Icon Theme", key, NULL, NULL);
	GPtrArray *roots;

	if (!directories)
		return;
	roots = g_ptr_array_new_with_free_func(g_free);
	for (unsigned int i = 0; i < lookup->bases->len; i++) {
		char *path = g_build_filename(g_ptr_array_index(lookup->bases, i), name, NULL);

		wApplicationsCacheWatchPath(lookup->cache, path);
		if (g_file_test(path, G_FILE_TEST_IS_DIR))
			g_ptr_array_add(roots, path);
		else
			g_free(path);
	}
	for (unsigned int i = 0; directories[i]; i++) {
		int size, scale, minimum, maximum;
		char *type;

		if (!relativePath(directories[i]))
			continue;
		size = g_key_file_get_integer(index, directories[i], "Size", NULL);
		scale = g_key_file_get_integer(index, directories[i], "Scale", NULL);
		scale = MAX(1, scale);
		if (size <= 0 || size > 4096 || scale > 16)
			continue;
		minimum = maximum = size;
		type = g_key_file_get_string(index, directories[i], "Type", NULL);
		if (g_strcmp0(type, "Scalable") == 0) {
			minimum = g_key_file_get_integer(index, directories[i], "MinSize", NULL);
			maximum = g_key_file_get_integer(index, directories[i], "MaxSize", NULL);
			if (minimum <= 0)
				minimum = size;
			if (maximum <= 0)
				maximum = size;
		} else if (!type || strcmp(type, "Threshold") == 0) {
			int threshold = 2;

			if (g_key_file_has_key(index, directories[i], "Threshold", NULL))
				threshold = g_key_file_get_integer(index, directories[i], "Threshold", NULL);
			threshold = CLAMP(threshold, 0, 4096);
			minimum = MAX(1, size - threshold);
			maximum = size + threshold;
		}
		g_free(type);
		minimum = CLAMP(minimum, 1, 4096) * scale;
		maximum = CLAMP(maximum, 1, 4096) * scale;
		for (unsigned int j = 0; j < roots->len; j++) {
			IconDirectory *directory = g_new0(IconDirectory, 1);

			directory->path = g_build_filename(g_ptr_array_index(roots, j), directories[i], NULL);
			directory->distance = MAX(MAX(minimum - lookup->size, lookup->size - maximum), 0);
			g_ptr_array_add(theme->directories, directory);
		}
	}
	g_strfreev(directories);
	g_ptr_array_unref(roots);
}

static IconTheme *loadTheme(IconLookup *lookup, const char *name)
{
	IconTheme *theme = g_hash_table_lookup(lookup->themes, name);
	GKeyFile *index;

	if (theme)
		return theme;
	theme = g_new0(IconTheme, 1);
	theme->directories = g_ptr_array_new_with_free_func(freeIconDirectory);
	g_hash_table_insert(lookup->themes, g_strdup(name), theme);
	if (!validThemeName(name))
		return theme;
	index = g_key_file_new();
	g_key_file_set_list_separator(index, ',');
	for (unsigned int i = 0; i < lookup->bases->len; i++) {
		char *path = g_build_filename(g_ptr_array_index(lookup->bases, i), name, "index.theme", NULL);
		GError *error = NULL;
		gboolean loaded;

		wApplicationsCacheWatchPath(lookup->cache, path);
		loaded = g_key_file_load_from_file(index, path, G_KEY_FILE_NONE, &error);

		if (!loaded && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			wwarning(_("could not read icon theme \"%s\": %s"), path, error->message);
		g_clear_error(&error);
		g_free(path);
		if (loaded) {
			theme->inherits = g_key_file_get_string_list(index, "Icon Theme", "Inherits", NULL, NULL);
			addThemeDirectories(lookup, theme, index, name, "Directories");
			addThemeDirectories(lookup, theme, index, name, "ScaledDirectories");
			break;
		}
	}
	g_key_file_unref(index);
	return theme;
}

static char *findIconFile(IconLookup *lookup, IconDirectory *directory, const char *name)
{
	static const char *extensions[] = { ".png", ".xpm",
#ifdef USE_MAGICK
		".svg",
#endif
		NULL
	};

	if (!directory->files) {
		GDir *dir;
		const char *file;

		wApplicationsCacheWatchPath(lookup->cache, directory->path);
		dir = g_dir_open(directory->path, 0, NULL);
		directory->files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		if (dir) {
			while ((file = g_dir_read_name(dir)))
				g_hash_table_add(directory->files, g_strdup(file));
			g_dir_close(dir);
		}
	}
	for (unsigned int i = 0; extensions[i]; i++) {
		char *file = g_strconcat(name, extensions[i], NULL);
		char *path = NULL;

		if (g_hash_table_contains(directory->files, file))
			path = g_build_filename(directory->path, file, NULL);
		g_free(file);
		if (path) {
			wApplicationsCacheWatchPath(lookup->cache, path);
			if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
				return path;
			g_free(path);
		}
	}
	return NULL;
}

static char *findThemeIcon(IconLookup *lookup, const char *themeName, const char *name, GHashTable *visited)
{
	IconTheme *theme;
	char *path = NULL;
	int distance = G_MAXINT;

	if (g_hash_table_contains(visited, themeName) || g_hash_table_size(visited) >= 32)
		return NULL;
	g_hash_table_add(visited, g_strdup(themeName));
	theme = loadTheme(lookup, themeName);
	for (unsigned int i = 0; i < theme->directories->len; i++) {
		IconDirectory *directory = g_ptr_array_index(theme->directories, i);
		char *candidate;

		if (directory->distance >= distance)
			continue;
		candidate = findIconFile(lookup, directory, name);
		if (candidate) {
			g_free(path);
			path = candidate;
			distance = directory->distance;
			if (distance == 0)
				return path;
		}
	}
	if (path)
		return path;
	for (unsigned int i = 0; theme->inherits && theme->inherits[i]; i++) {
		path = findThemeIcon(lookup, theme->inherits[i], name, visited);
		if (path)
			return path;
	}
	return NULL;
}

static char *findNamedIcon(IconLookup *lookup, const char *name)
{
	char *path;
	GHashTable *visited;

	if (!relativePath(name) || strchr(name, '/'))
		return NULL;
	visited = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	path = findThemeIcon(lookup, lookup->name, name, visited);
	if (!path)
		path = findThemeIcon(lookup, "hicolor", name, visited);
	g_hash_table_unref(visited);
	for (unsigned int i = 0; !path && i < lookup->fallbacks->len; i++)
		path = findIconFile(lookup, g_ptr_array_index(lookup->fallbacks, i), name);
	return path;
}

static const char *resolveIcon(IconLookup *lookup, GIcon *icon)
{
	char *key, *path = NULL;

	if (!icon)
		return NULL;
	key = g_icon_to_string(icon);
	if (!key)
		return NULL;
	if (g_hash_table_contains(lookup->icons, key)) {
		const char *cached = g_hash_table_lookup(lookup->icons, key);

		g_free(key);
		return cached;
	}
	if (G_IS_FILE_ICON(icon)) {
		path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(icon)));
		wApplicationsCacheWatchPath(lookup->cache, path);
		if (path && !g_file_test(path, G_FILE_TEST_IS_REGULAR))
			g_clear_pointer(&path, g_free);
	} else if (G_IS_THEMED_ICON(icon)) {
		const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));

		for (unsigned int i = 0; names[i] && !path; i++)
			path = findNamedIcon(lookup, names[i]);
	}
	g_hash_table_insert(lookup->icons, key, path);
	return path;
}

static void initializeIcons(IconLookup *lookup, const char *name, int size)
{
	const char *const *dataDirs = g_get_system_data_dirs();

	memset(lookup, 0, sizeof(*lookup));
	lookup->bases = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(lookup->bases, g_build_filename(g_get_user_data_dir(), "icons", NULL));
	g_ptr_array_add(lookup->bases, g_build_filename(g_get_home_dir(), ".icons", NULL));
	for (unsigned int i = 0; dataDirs[i]; i++)
		g_ptr_array_add(lookup->bases, g_build_filename(dataDirs[i], "icons", NULL));
	g_ptr_array_add(lookup->bases, g_build_filename(g_get_user_data_dir(), "pixmaps", NULL));
	for (unsigned int i = 0; dataDirs[i]; i++)
		g_ptr_array_add(lookup->bases, g_build_filename(dataDirs[i], "pixmaps", NULL));
	lookup->fallbacks = g_ptr_array_new_with_free_func(freeIconDirectory);
	for (unsigned int i = 0; i < lookup->bases->len; i++) {
		IconDirectory *directory = g_new0(IconDirectory, 1);

		directory->path = g_strdup(g_ptr_array_index(lookup->bases, i));
		g_ptr_array_add(lookup->fallbacks, directory);
	}
	lookup->themes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, freeIconTheme);
	lookup->icons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	lookup->name = g_strdup(name);
	lookup->size = size;
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

static WMPropList *applicationEntry(GAppInfo *app, IconLookup *icons)
{
	WMPropList *entry = WMCreatePLArray(NULL);
	const char *icon = icons ? resolveIcon(icons, g_app_info_get_icon(app)) : NULL;
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

static char *configuredIconTheme(void)
{
	static const char *versions[] = { "gtk-4.0", "gtk-3.0" };
	char *theme = NULL;

	for (unsigned int i = 0; i < sizeof(versions) / sizeof(versions[0]) && !theme; i++) {
		GKeyFile *settings = g_key_file_new();
		char *path = g_build_filename(g_get_user_config_dir(), versions[i], "settings.ini", NULL);
		GError *error = NULL;

		if (g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, &error))
			theme = g_key_file_get_string(settings, "Settings", "gtk-icon-theme-name", NULL);
		else if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			wwarning(_("could not read GTK settings \"%s\": %s"), path, error->message);
		g_clear_error(&error);
		g_free(path);
		g_key_file_unref(settings);
		if (theme && !validThemeName(theme)) {
			wwarning(_("invalid GTK icon theme \"%s\""), theme);
			g_clear_pointer(&theme, g_free);
		}
	}
	if (!theme) {
		char *path = g_build_filename(g_get_home_dir(), ".gtkrc-2.0", NULL);
		char *contents = NULL;
		GError *error = NULL;

		if (g_file_get_contents(path, &contents, NULL, &error)) {
			GRegex *regex = g_regex_new("^\\s*gtk-icon-theme-name\\s*=\\s*\"([^\"\\r\\n]+)\"",
						    G_REGEX_MULTILINE, 0, NULL);
			GMatchInfo *match = NULL;

			if (g_regex_match(regex, contents, 0, &match))
				theme = g_match_info_fetch(match, 1);
			g_match_info_free(match);
			g_regex_unref(regex);
		} else if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			wwarning(_("could not read GTK settings \"%s\": %s"), path, error->message);
		g_clear_error(&error);
		g_free(contents);
		g_free(path);
	}
	if (theme && !validThemeName(theme)) {
		wwarning(_("invalid GTK icon theme \"%s\""), theme);
		g_clear_pointer(&theme, g_free);
	}
	return theme ? theme : g_strdup("hicolor");
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
	    (themeName && !validThemeName(themeName)) || iconSize < 1 || iconSize > 512 ||
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
		IconLookup icons;
		WMPropList *result;
		GHashTable *submenus;
		WMApplicationsCache *cache;
		char *description;

		if (!g_getenv("XDG_CURRENT_DESKTOP"))
			g_setenv("XDG_CURRENT_DESKTOP", "WindowMaker", FALSE);
		if (!themeName)
			themeName = configuredIconTheme();
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
		initializeIcons(&icons, themeName, iconSize);
		icons.cache = cache;
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
			entry = applicationEntry(app, noIcons ? NULL : &icons);
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
		g_hash_table_unref(icons.icons);
		g_hash_table_unref(icons.themes);
		g_ptr_array_unref(icons.fallbacks);
		g_ptr_array_unref(icons.bases);
		g_free(icons.name);
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
