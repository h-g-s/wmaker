/* Standard desktop application associations for wmapplications.
 * Copyright (C) 2026 Window Maker contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ApplicationDefaults.h"

#ifdef HAVE_GIO
#include <errno.h>
#include <string.h>
#include <glib/gstdio.h>

static const char *const browserTypes[] = {
	"x-scheme-handler/http", "x-scheme-handler/https", "text/html", NULL
};
static const char *const emailTypes[] = { "x-scheme-handler/mailto", NULL };
static const char *const fileTypes[] = { "inode/directory", NULL };
static const char *const archiveTypes[] = {
	"application/zip", "application/x-tar", "application/gzip", "application/x-gzip",
	"application/x-bzip", "application/x-bzip2", "application/x-xz",
	"application/x-compressed-tar", "application/x-bzip-compressed-tar",
	"application/x-xz-compressed-tar", "application/x-7z-compressed",
	"application/vnd.rar", "application/x-rar", "application/x-rar-compressed",
	"application/zstd", "application/x-zstd-compressed-tar", NULL
};

static const char *const *roleTypes(WMDefaultRole role)
{
	switch (role) {
	case WMDefaultBrowser: return browserTypes;
	case WMDefaultEmail: return emailTypes;
	case WMDefaultFileManager: return fileTypes;
	case WMDefaultArchiveManager: return archiveTypes;
	default: return NULL;
	}
}

static gboolean supportsType(GAppInfo *app, const char *type)
{
	const char *const *types = g_app_info_get_supported_types(app);

	for (int i = 0; types && types[i]; i++)
		if (strcmp(types[i], type) == 0)
			return TRUE;
	return FALSE;
}

static gboolean eligible(WMDefaultRole role, GAppInfo *app)
{
	const char *const *types = roleTypes(role);

	if (!G_IS_DESKTOP_APP_INFO(app) || !g_app_info_get_id(app) ||
	    g_desktop_app_info_get_is_hidden(G_DESKTOP_APP_INFO(app)))
		return FALSE;
	if (role == WMDefaultTerminal) {
		const char *categories = g_desktop_app_info_get_categories(G_DESKTOP_APP_INFO(app));
		char **items = g_strsplit(categories ? categories : "", ";", -1);
		gboolean terminal = g_strv_contains((const char *const *)items, "TerminalEmulator");

		g_strfreev(items);
		return terminal && !strpbrk(g_app_info_get_id(app), "\r\n:") &&
			(g_desktop_app_info_has_key(G_DESKTOP_APP_INFO(app), "X-TerminalArgExec") ||
			 g_desktop_app_info_has_key(G_DESKTOP_APP_INFO(app), "TerminalArgExec"));
	}
	for (int i = 0; types && types[i]; i++)
		if (supportsType(app, types[i]))
			return TRUE;
	return FALSE;
}

static gint compareApps(gconstpointer left, gconstpointer right)
{
	GAppInfo *a = *(GAppInfo *const *)left;
	GAppInfo *b = *(GAppInfo *const *)right;
	int result = g_utf8_collate(g_app_info_get_display_name(a), g_app_info_get_display_name(b));

	return result ? result : g_strcmp0(g_app_info_get_id(a), g_app_info_get_id(b));
}

GPtrArray *WMDefaultApplications(WMDefaultRole role)
{
	GPtrArray *apps = g_ptr_array_new_with_free_func(g_object_unref);
	GList *all = g_app_info_get_all();

	for (GList *item = all; item; item = item->next)
		if (eligible(role, item->data) &&
		    !g_desktop_app_info_get_nodisplay(G_DESKTOP_APP_INFO(item->data)))
			g_ptr_array_add(apps, g_object_ref(item->data));
	g_list_free_full(all, g_object_unref);
	g_ptr_array_sort(apps, compareApps);
	return apps;
}

char *WMDefaultApplicationID(WMDefaultRole role, GError **error)
{
	const char *const *types = roleTypes(role);
	char *id = NULL;

	if (role == WMDefaultTerminal) {
		char *argv[] = { "xdg-terminal-exec", "--print-id", NULL };
		char *output = NULL, *diagnostic = NULL;
		int status;

		if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
				  &output, &diagnostic, &status, error))
			return NULL;
		if (!g_spawn_check_wait_status(status, error)) {
			if (diagnostic && *diagnostic)
				g_prefix_error(error, "%s: ", g_strstrip(diagnostic));
			g_free(output);
			output = NULL;
		} else {
			g_strstrip(output);
		}
		g_free(diagnostic);
		return output;
	}
	for (int i = 0; types && types[i]; i++) {
		GAppInfo *app = g_app_info_get_default_for_type(types[i], FALSE);
		const char *current = app ? g_app_info_get_id(app) : NULL;

		/* Mixed defaults must not become a single app merely by pressing Apply. */
		if (!current || (id && strcmp(id, current) != 0)) {
			g_free(id);
			if (app)
				g_object_unref(app);
			return NULL;
		}
		if (!id)
			id = g_strdup(current);
		g_object_unref(app);
	}
	return id;
}

static char *terminalConfigPath(void)
{
	const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
	char **desktops = g_strsplit(desktop ? desktop : "", ":", -1);
	char *filename = g_strdup("xdg-terminals.list");
	char *path;

	/* The first desktop-specific file takes precedence over the generic file. */
	for (int i = 0; desktops[i]; i++) {
		gboolean safe = *desktops[i] != '\0';

		for (const char *p = desktops[i]; *p; p++)
			if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_')
				safe = FALSE;
		if (safe) {
			char *lower = g_ascii_strdown(desktops[i], -1);

			g_free(filename);
			filename = g_strconcat(lower, "-xdg-terminals.list", NULL);
			g_free(lower);
			break;
		}
	}
	path = g_build_filename(g_get_user_config_dir(), filename, NULL);
	g_strfreev(desktops);
	g_free(filename);
	return path;
}

static gboolean setTerminal(GAppInfo *app, GError **error)
{
	char *path = terminalConfigPath();
	char *previous = NULL, *contents;
	GError *readError = NULL;
	gboolean result = FALSE;

	if (!g_file_get_contents(path, &previous, NULL, &readError)) {
		if (!g_error_matches(readError, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
			g_propagate_error(error, readError);
			goto done;
		}
		g_clear_error(&readError);
	}
	if (g_mkdir_with_parents(g_get_user_config_dir(), 0700) != 0) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
			    _("Cannot create %s: %s"), g_get_user_config_dir(), g_strerror(errno));
		goto done;
	}
	/* Keep comments, directives, exclusions and earlier fallback preferences. */
	contents = g_strconcat(g_app_info_get_id(app), "\n", previous ? previous : "", NULL);
	result = g_file_set_contents(path, contents, -1, error);
	g_free(contents);
done:
	g_free(previous);
	g_free(path);
	return result;
}

gboolean WMSetDefaultApplication(WMDefaultRole role, GAppInfo *app, GError **error)
{
	const char *const *types = roleTypes(role);

	if (!eligible(role, app)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				    _("Application does not support this role"));
		return FALSE;
	}
	if (role == WMDefaultTerminal)
		return setTerminal(app, error);
	for (int i = 0; types && types[i]; i++) {
		if (!supportsType(app, types[i]))
			continue;
		if (!g_app_info_set_as_default_for_type(app, types[i], error)) {
			g_prefix_error(error, "%s: ", types[i]);
			return FALSE;
		}
	}
	return TRUE;
}
#endif
