/* Desktop-entry matching for dock launchers. */
#include "wconfig.h"
#include "desktop_entry.h"

#ifdef HAVE_GIO
#include <gio/gdesktopappinfo.h>
#include <WINGs/WUtil.h>
#include <string.h>

static int idMatches(const char *id, const char *name, gboolean insensitive)
{
	size_t length;

	if (!id || !name || !*name || !g_str_has_suffix(id, ".desktop"))
		return 0;
	length = strlen(id) - strlen(".desktop");
	return strlen(name) == length &&
		(insensitive ? g_ascii_strncasecmp(id, name, length) == 0 : strncmp(id, name, length) == 0);
}

static int matchScore(GDesktopAppInfo *app, const char *application_id,
		      const char *instance, const char *class_name)
{
	const char *id = g_app_info_get_id(G_APP_INFO(app));
	const char *startup = g_desktop_app_info_get_startup_wm_class(app);

	if (g_desktop_app_info_get_is_hidden(app))
		return 0;
	if (idMatches(id, application_id, FALSE))
		return 4;
	if (idMatches(id, class_name, FALSE) || idMatches(id, instance, FALSE))
		return 3;
	if (startup && ((class_name && strcmp(startup, class_name) == 0) ||
			(instance && strcmp(startup, instance) == 0)))
		return 3;
	if (idMatches(id, class_name, TRUE) || idMatches(id, instance, TRUE))
		return 1;
	return 0;
}

static char *launcherCommand(const char *id)
{
	GString *command = g_string_new("wmmenugen --launch '");
	char *result;

	for (const char *p = id; *p; p++) {
		if (*p == '\\')
			g_string_append(command, "\\\\\\\\");
		else if (*p == '\'')
			g_string_append(command, "\\\\'");
		else if (*p == '%')
			g_string_append(command, "\\%");
		else
			g_string_append_c(command, *p);
	}
	g_string_append_c(command, '\'');
	result = wstrdup(command->str);
	g_string_free(command, TRUE);
	return result;
}

char *wDesktopEntryCommand(const char *application_id, const char *instance, const char *class_name)
{
	GList *apps = g_app_info_get_all();
	const char *best = NULL;
	int bestScore = 0;
	gboolean ambiguous = FALSE;
	char *command = NULL;

	for (GList *item = apps; item; item = item->next) {
		GAppInfo *app = item->data;
		int score;

		if (!G_IS_DESKTOP_APP_INFO(app) || !g_app_info_get_id(app))
			continue;
		score = matchScore(G_DESKTOP_APP_INFO(app), application_id, instance, class_name);
		if (score > bestScore) {
			bestScore = score;
			best = g_app_info_get_id(app);
			ambiguous = FALSE;
		} else if (score && score == bestScore && g_strcmp0(best, g_app_info_get_id(app)) != 0) {
			ambiguous = TRUE;
		}
	}
	if (best && !ambiguous)
		command = launcherCommand(best);
	g_list_free_full(apps, g_object_unref);
	return command;
}

char *wDesktopEntryResolve(const char *application_id, const char *instance, const char *class_name)
{
	char *argv[] = { "wmmenugen", "--resolve-window", "--",
			 (char *)(application_id ? application_id : ""),
			 (char *)(instance ? instance : ""),
			 (char *)(class_name ? class_name : ""), NULL };
	char *output = NULL, *result = NULL;
	GError *error = NULL;
	int status;

	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &output, NULL, &status, &error) ||
	    !g_spawn_check_exit_status(status, &error)) {
		wwarning(_("could not look up desktop application: %s"), error->message);
		g_clear_error(&error);
	} else if (output && *output) {
		result = wstrdup(output);
	}
	g_free(output);
	return result;
}

int wDesktopCommandExecutable(const char *command)
{
	char **argv = NULL, *found;
	int argc = 0, result;

	if (!command || !*command)
		return 0;
	wtokensplit((char *)command, &argv, &argc);
	if (!argc)
		return 0;
	found = g_find_program_in_path(argv[0]);
	result = found != NULL;
	g_free(found);
	wtokenfree(argv, argc);
	return result;
}
#else
char *wDesktopEntryCommand(const char *application_id, const char *instance, const char *class_name)
{
	(void)application_id;
	(void)instance;
	(void)class_name;
	return 0;
}

char *wDesktopEntryResolve(const char *application_id, const char *instance, const char *class_name)
{
	(void)application_id;
	(void)instance;
	(void)class_name;
	return 0;
}

int wDesktopCommandExecutable(const char *command)
{
	return command && *command;
}
#endif
