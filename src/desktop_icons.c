/* Shared desktop icon-theme lookup.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "desktop_icons.h"

#ifdef HAVE_GIO
#include <WINGs/WUtil.h>
#include <string.h>

typedef struct {
	char *path;
	int distance;
	GHashTable *files;
} IconDirectory;

typedef struct {
	GPtrArray *directories;
	char **inherits;
} IconTheme;

struct WMDesktopIcons {
	GPtrArray *bases;
	GPtrArray *fallbacks;
	GHashTable *themes;
	GHashTable *icons;
	char *name;
	int size;
	WMDesktopIconWatch watch;
	void *watchData;
};

static void watchPath(WMDesktopIcons *lookup, const char *path)
{
	if (lookup->watch)
		lookup->watch(lookup->watchData, path);
}

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

gboolean wDesktopIconThemeValid(const char *name)
{
	if (!relativePath(name) || strchr(name, '/') || strcmp(name, ".") == 0)
		return FALSE;
	for (const unsigned char *p = (const unsigned char *)name; *p; p++)
		if (*p <= ' ' || *p >= 127 || *p == ',')
			return FALSE;
	return TRUE;
}

static void addThemeDirectories(WMDesktopIcons *lookup, IconTheme *theme, GKeyFile *index,
				const char *name, const char *key)
{
	char **directories = g_key_file_get_string_list(index, "Icon Theme", key, NULL, NULL);
	GPtrArray *roots;

	if (!directories)
		return;
	roots = g_ptr_array_new_with_free_func(g_free);
	for (unsigned int i = 0; i < lookup->bases->len; i++) {
		char *path = g_build_filename(g_ptr_array_index(lookup->bases, i), name, NULL);

		watchPath(lookup, path);
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

static IconTheme *loadTheme(WMDesktopIcons *lookup, const char *name)
{
	IconTheme *theme = g_hash_table_lookup(lookup->themes, name);
	GKeyFile *index;

	if (theme)
		return theme;
	theme = g_new0(IconTheme, 1);
	theme->directories = g_ptr_array_new_with_free_func(freeIconDirectory);
	g_hash_table_insert(lookup->themes, g_strdup(name), theme);
	if (!wDesktopIconThemeValid(name))
		return theme;
	index = g_key_file_new();
	g_key_file_set_list_separator(index, ',');
	for (unsigned int i = 0; i < lookup->bases->len; i++) {
		char *path = g_build_filename(g_ptr_array_index(lookup->bases, i), name, "index.theme", NULL);
		GError *error = NULL;
		gboolean loaded;

		watchPath(lookup, path);
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

static char *findIconFile(WMDesktopIcons *lookup, IconDirectory *directory, const char *name)
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

		watchPath(lookup, directory->path);
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
			watchPath(lookup, path);
			if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
				return path;
			g_free(path);
		}
	}
	return NULL;
}

static char *findThemeIcon(WMDesktopIcons *lookup, const char *themeName, const char *name, GHashTable *visited)
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

static char *findNamedIcon(WMDesktopIcons *lookup, const char *name)
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

const char *wDesktopIconsResolve(WMDesktopIcons *lookup, GIcon *icon)
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
		watchPath(lookup, path);
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

WMDesktopIcons *wDesktopIconsCreate(const char *name, int size, WMDesktopIconWatch watch, void *data)
{
	const char *const *dataDirs = g_get_system_data_dirs();
	WMDesktopIcons *lookup = g_new0(WMDesktopIcons, 1);

	lookup->watch = watch;
	lookup->watchData = data;
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
	lookup->name = name ? g_strdup(name) : wDesktopIconTheme();
	lookup->size = size;
	return lookup;
}

char *wDesktopIconTheme(void)
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
		if (theme && !wDesktopIconThemeValid(theme)) {
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
	if (theme && !wDesktopIconThemeValid(theme)) {
		wwarning(_("invalid GTK icon theme \"%s\""), theme);
		g_clear_pointer(&theme, g_free);
	}
	return theme ? theme : g_strdup("hicolor");
}

void wDesktopIconsDestroy(WMDesktopIcons *icons)
{
	g_hash_table_unref(icons->icons);
	g_hash_table_unref(icons->themes);
	g_ptr_array_unref(icons->fallbacks);
	g_ptr_array_unref(icons->bases);
	g_free(icons->name);
	g_free(icons);
}
#endif
