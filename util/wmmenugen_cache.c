#include <sys/stat.h>
#include <ftw.h>
#include "wmmenugen.h"

#ifdef HAVE_GIO
#include <errno.h>
#include <unistd.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include "wmmenugen_cache.h"

#define CACHE_FORMAT "1"

struct WMApplicationsCache {
	char *filename;
	char *program;
	char *inputs;
	GPtrArray *desktopFiles;
	GHashTable *paths;
	GHashTable *programs;
	gboolean usable;
};

static void checksumString(GChecksum *checksum, const char *text)
{
	g_checksum_update(checksum, (const guchar *)(text ? text : ""), strlen(text ? text : "") + 1);
}

static char *pathState(WMApplicationsCache *cache, const char *path)
{
	struct stat st;
	long mtimeNS = 0, ctimeNS = 0;

	if (g_stat(path, &st) != 0) {
		if (errno == ENOENT || errno == ENOTDIR)
			return g_strdup("missing");
		wwarning(_("could not validate menu cache dependency \"%s\": %s"), path, g_strerror(errno));
		cache->usable = FALSE;
		return g_strdup("unreadable");
	}
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	mtimeNS = st.st_mtim.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	mtimeNS = st.st_mtimespec.tv_nsec;
#endif
#if defined(HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC)
	ctimeNS = st.st_ctim.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_CTIMESPEC_TV_NSEC)
	ctimeNS = st.st_ctimespec.tv_nsec;
#endif
	return g_strdup_printf("%" G_GUINT64_FORMAT ":%" G_GUINT64_FORMAT ":%" G_GINT64_FORMAT
			       ":%" G_GINT64_FORMAT ":%ld:%" G_GINT64_FORMAT ":%ld:%u:%u:%u:%d:%d",
			       (guint64)st.st_dev, (guint64)st.st_ino, (gint64)st.st_size,
			       (gint64)st.st_mtime, mtimeNS, (gint64)st.st_ctime, ctimeNS,
			       (unsigned int)st.st_mode, (unsigned int)st.st_uid, (unsigned int)st.st_gid,
			       g_access(path, R_OK) == 0, g_access(path, X_OK) == 0);
}

static gint compareNames(gconstpointer left, gconstpointer right)
{
	return strcmp(*(char *const *)left, *(char *const *)right);
}

static void scanDirectory(WMApplicationsCache *cache, GChecksum *checksum, const char *path,
			  GHashTable *visited, gboolean collect, unsigned int depth)
{
	char *state = pathState(cache, path);
	struct stat st;
	GDir *directory;
	GError *error = NULL;
	GPtrArray *names;
	const char *name;
	char *identity;

	checksumString(checksum, path);
	checksumString(checksum, state);
	g_free(state);
	if (g_stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
		return;
	identity = g_strdup_printf("%" G_GUINT64_FORMAT ":%" G_GUINT64_FORMAT,
				   (guint64)st.st_dev, (guint64)st.st_ino);
	if (g_hash_table_contains(visited, identity)) {
		g_free(identity);
		return;
	}
	g_hash_table_add(visited, identity);
	if (depth > 64) {
		wwarning(_("application directory nesting is too deep to cache: \"%s\""), path);
		cache->usable = FALSE;
		return;
	}
	directory = g_dir_open(path, 0, &error);
	if (!directory) {
		wwarning(_("could not scan application directory \"%s\": %s"), path, error->message);
		g_clear_error(&error);
		cache->usable = FALSE;
		return;
	}
	names = g_ptr_array_new_with_free_func(g_free);
	while ((name = g_dir_read_name(directory)))
		g_ptr_array_add(names, g_strdup(name));
	g_dir_close(directory);
	g_ptr_array_sort(names, compareNames);
	for (unsigned int i = 0; i < names->len; i++) {
		char *child = g_build_filename(path, g_ptr_array_index(names, i), NULL);
		int statStatus = g_stat(child, &st);

		if (statStatus != 0 && errno != ENOENT && errno != ENOTDIR) {
			wwarning(_("could not inspect application path \"%s\": %s"), child, g_strerror(errno));
			cache->usable = FALSE;
		} else if (statStatus == 0 && S_ISDIR(st.st_mode))
			scanDirectory(cache, checksum, child, visited, collect, depth + 1);
		else if (g_str_has_suffix(child, ".desktop")) {
			state = pathState(cache, child);
			checksumString(checksum, child);
			checksumString(checksum, state);
			g_free(state);
			if (collect)
				g_ptr_array_add(cache->desktopFiles, g_strdup(child));
		}
		g_free(child);
	}
	g_ptr_array_unref(names);
}

static char *inputFingerprint(WMApplicationsCache *cache, gboolean collect)
{
	GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
	GHashTable *visited = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const char *const *directories = g_get_system_data_dirs();
	char *path = g_build_filename(g_get_user_data_dir(), "applications", NULL);
	char *state, *result;

	scanDirectory(cache, checksum, path, visited, collect, 0);
	g_free(path);
	for (unsigned int i = 0; directories[i]; i++) {
		path = g_build_filename(directories[i], "applications", NULL);
		scanDirectory(cache, checksum, path, visited, collect, 0);
		g_free(path);
	}
	state = pathState(cache, cache->program);
	checksumString(checksum, cache->program);
	checksumString(checksum, state);
	g_free(state);
	result = g_strdup(g_checksum_get_string(checksum));
	g_checksum_free(checksum);
	g_hash_table_unref(visited);
	return result;
}

static char *programState(WMApplicationsCache *cache, const char *name)
{
	char *path = g_find_program_in_path(name);
	char *state, *result;

	if (!path)
		return g_strdup("missing");
	state = pathState(cache, path);
	result = g_strconcat(path, "\n", state, NULL);
	g_free(path);
	g_free(state);
	return result;
}

WMApplicationsCache *wApplicationsCacheCreate(const char *program, const char *category,
					      const char *theme, int size, gboolean noIcons)
{
#if !defined(HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC) && !defined(HAVE_STRUCT_STAT_ST_CTIMESPEC_TV_NSEC)
	(void)program;
	(void)category;
	(void)theme;
	(void)size;
	(void)noIcons;
	wwarning(_("high-resolution file timestamps are unavailable; menu caching is disabled"));
	return NULL;
#else
	static const char *variables[] = {
		"PATH", "LANG", "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LC_COLLATE", "LC_CTYPE",
		"XDG_CURRENT_DESKTOP", "XDG_DATA_HOME", "XDG_DATA_DIRS"
	};
	WMApplicationsCache *cache = g_new0(WMApplicationsCache, 1);
	GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
	char *options = g_strdup_printf("%s:%s:%u.%u.%u:%d:%d:%u", CACHE_FORMAT, VERSION,
					glib_major_version, glib_minor_version, glib_micro_version,
					size, noIcons, (unsigned int)geteuid());
	char *file;

	checksumString(checksum, options);
	checksumString(checksum, category);
	checksumString(checksum, theme);
	checksumString(checksum, g_get_home_dir());
	for (unsigned int i = 0; i < sizeof(variables) / sizeof(variables[0]); i++)
		checksumString(checksum, g_getenv(variables[i]));
	file = g_strconcat("applications-", g_checksum_get_string(checksum), ".cache", NULL);
	cache->filename = g_build_filename(g_get_user_cache_dir(), "WindowMaker", "menus", file, NULL);
	cache->program = g_find_program_in_path(program);
	if (!cache->program)
		cache->program = g_strdup(program);
	cache->desktopFiles = g_ptr_array_new_with_free_func(g_free);
	cache->paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	cache->programs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	cache->usable = TRUE;
	cache->inputs = inputFingerprint(cache, TRUE);
	g_free(file);
	g_free(options);
	g_checksum_free(checksum);
	return cache;
#endif
}

void wApplicationsCacheWatchPath(WMApplicationsCache *cache, const char *path)
{
	if (cache && path && !g_hash_table_contains(cache->paths, path))
		g_hash_table_insert(cache->paths, g_strdup(path), pathState(cache, path));
}

void wApplicationsCachePrepare(WMApplicationsCache *cache)
{
	if (!cache)
		return;
	for (unsigned int i = 0; i < cache->desktopFiles->len; i++) {
		GKeyFile *desktop = g_key_file_new();
		const char *path = g_ptr_array_index(cache->desktopFiles, i);

		if (g_key_file_load_from_file(desktop, path, G_KEY_FILE_NONE, NULL)) {
			char *tryExec = g_key_file_get_string(desktop, "Desktop Entry", "TryExec", NULL);
			char *exec = g_key_file_get_string(desktop, "Desktop Entry", "Exec", NULL);
			char **arguments = NULL;

			if (tryExec && !g_hash_table_contains(cache->programs, tryExec))
				g_hash_table_insert(cache->programs, g_strdup(tryExec), programState(cache, tryExec));
			if (exec && g_shell_parse_argv(exec, NULL, &arguments, NULL) && arguments[0] &&
			    !g_hash_table_contains(cache->programs, arguments[0]))
				g_hash_table_insert(cache->programs, g_strdup(arguments[0]),
						    programState(cache, arguments[0]));
			g_strfreev(arguments);
			g_free(exec);
			g_free(tryExec);
		}
		g_key_file_unref(desktop);
	}
}

static char *dependencyFingerprint(WMApplicationsCache *cache, char **paths, char **programs,
				  gboolean snapshots)
{
	GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
	char *state, *result;

	checksumString(checksum, cache->inputs);
	for (unsigned int i = 0; paths[i]; i++) {
		state = snapshots ? g_strdup(g_hash_table_lookup(cache->paths, paths[i])) : pathState(cache, paths[i]);
		checksumString(checksum, paths[i]);
		checksumString(checksum, state);
		g_free(state);
	}
	for (unsigned int i = 0; programs[i]; i++) {
		state = snapshots ? g_strdup(g_hash_table_lookup(cache->programs, programs[i])) :
			programState(cache, programs[i]);
		checksumString(checksum, programs[i]);
		checksumString(checksum, state);
		g_free(state);
	}
	result = g_strdup(g_checksum_get_string(checksum));
	g_checksum_free(checksum);
	return result;
}

char *wApplicationsCacheLoad(WMApplicationsCache *cache)
{
	GKeyFile *file;
	GError *error = NULL;
	char **paths = NULL, **programs = NULL;
	char *format = NULL, *fingerprint = NULL, *current = NULL, *menu = NULL, *hash = NULL, *actual = NULL;
	struct stat st;

	if (!cache || !cache->usable)
		return NULL;
	if (g_lstat(cache->filename, &st) != 0) {
		if (errno != ENOENT && errno != ENOTDIR)
			wwarning(_("could not read menu cache \"%s\": %s"), cache->filename, g_strerror(errno));
		return NULL;
	}
	if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
		wwarning(_("ignoring non-private menu cache \"%s\""), cache->filename);
		return NULL;
	}
	file = g_key_file_new();
	if (!g_key_file_load_from_file(file, cache->filename, G_KEY_FILE_NONE, &error)) {
		wwarning(_("could not read menu cache \"%s\": %s"), cache->filename, error->message);
		g_clear_error(&error);
		goto finish;
	}
	format = g_key_file_get_string(file, "Cache", "Format", NULL);
	fingerprint = g_key_file_get_string(file, "Cache", "Fingerprint", NULL);
	paths = g_key_file_get_string_list(file, "Cache", "Paths", NULL, NULL);
	programs = g_key_file_get_string_list(file, "Cache", "Programs", NULL, NULL);
	hash = g_key_file_get_string(file, "Cache", "MenuHash", NULL);
	menu = g_key_file_get_string(file, "Cache", "Menu", NULL);
	if (g_strcmp0(format, CACHE_FORMAT) != 0 || !fingerprint || !paths || !programs || !hash || !menu) {
		wwarning(_("ignoring invalid menu cache \"%s\""), cache->filename);
		g_clear_pointer(&menu, g_free);
		goto finish;
	}
	current = dependencyFingerprint(cache, paths, programs, FALSE);
	actual = g_compute_checksum_for_string(G_CHECKSUM_SHA256, menu, -1);
	if (strcmp(hash, actual) != 0) {
		wwarning(_("ignoring corrupted menu cache \"%s\""), cache->filename);
		g_clear_pointer(&menu, g_free);
	} else if (!cache->usable || strcmp(fingerprint, current) != 0)
		g_clear_pointer(&menu, g_free);
finish:
	g_key_file_unref(file);
	g_strfreev(paths);
	g_strfreev(programs);
	g_free(format);
	g_free(fingerprint);
	g_free(current);
	g_free(hash);
	g_free(actual);
	return menu;
}

static char **sortedKeys(GHashTable *table)
{
	GList *keys = g_list_sort(g_hash_table_get_keys(table), (GCompareFunc)g_strcmp0);
	char **result = g_new0(char *, g_list_length(keys) + 1);
	unsigned int i = 0;

	for (GList *key = keys; key; key = key->next)
		result[i++] = g_strdup(key->data);
	g_list_free(keys);
	return result;
}

static gboolean dependenciesUnchanged(WMApplicationsCache *cache, GHashTable *table, gboolean programs)
{
	GHashTableIter iterator;
	gpointer path, before;

	g_hash_table_iter_init(&iterator, table);
	while (g_hash_table_iter_next(&iterator, &path, &before)) {
		char *after = programs ? programState(cache, path) : pathState(cache, path);
		gboolean same = strcmp(before, after) == 0;

		g_free(after);
		if (!same)
			return FALSE;
	}
	return TRUE;
}

void wApplicationsCacheSave(WMApplicationsCache *cache, const char *menu)
{
	char *inputs, *directory, **paths, **programs, *fingerprint, *hash, *data;
	GKeyFile *file;
	GFile *destination;
	GError *error = NULL;
	gsize length;
	struct stat st;

	if (!cache || !cache->usable)
		return;
	inputs = inputFingerprint(cache, FALSE);
	if (strcmp(inputs, cache->inputs) != 0 || !cache->usable ||
	    !dependenciesUnchanged(cache, cache->paths, FALSE) ||
	    !dependenciesUnchanged(cache, cache->programs, TRUE)) {
		g_free(inputs);
		return;
	}
	g_free(inputs);
	directory = g_path_get_dirname(cache->filename);
	if (g_mkdir_with_parents(directory, 0700) != 0 || g_lstat(directory, &st) != 0 ||
	    !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
		wwarning(_("could not use private menu cache directory \"%s\""), directory);
		g_free(directory);
		return;
	}
	g_free(directory);
	paths = sortedKeys(cache->paths);
	programs = sortedKeys(cache->programs);
	/* Keep the pre-read snapshots: a concurrent edit must not validate stale output. */
	fingerprint = dependencyFingerprint(cache, paths, programs, TRUE);
	hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, menu, -1);
	file = g_key_file_new();
	g_key_file_set_string(file, "Cache", "Format", CACHE_FORMAT);
	g_key_file_set_string(file, "Cache", "Fingerprint", fingerprint);
	g_key_file_set_string_list(file, "Cache", "Paths", (const char *const *)paths, g_strv_length(paths));
	g_key_file_set_string_list(file, "Cache", "Programs", (const char *const *)programs, g_strv_length(programs));
	g_key_file_set_string(file, "Cache", "MenuHash", hash);
	g_key_file_set_string(file, "Cache", "Menu", menu);
	data = g_key_file_to_data(file, &length, NULL);
	destination = g_file_new_for_path(cache->filename);
	if (!g_file_replace_contents(destination, data, length, NULL, FALSE,
				     G_FILE_CREATE_PRIVATE | G_FILE_CREATE_REPLACE_DESTINATION,
				     NULL, NULL, &error)) {
		wwarning(_("could not write menu cache \"%s\": %s"), cache->filename, error->message);
		g_clear_error(&error);
	}
	g_object_unref(destination);
	g_free(data);
	g_key_file_unref(file);
	g_free(fingerprint);
	g_free(hash);
	g_strfreev(paths);
	g_strfreev(programs);
}

void wApplicationsCacheDestroy(WMApplicationsCache *cache)
{
	if (!cache)
		return;
	g_free(cache->filename);
	g_free(cache->program);
	g_free(cache->inputs);
	g_ptr_array_unref(cache->desktopFiles);
	g_hash_table_unref(cache->paths);
	g_hash_table_unref(cache->programs);
	g_free(cache);
}
#endif
