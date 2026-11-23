/* autostart.c - commands launched when Window Maker starts
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
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "wconfig.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "WindowMaker.h"
#include "autostart.h"
#include "window.h"
#include "main.h"

#define MAX_AUTOSTART_DELAY 86400
#define LOW_PRIORITY_NICE 10
#define HIGH_PRIORITY_STEP 5

typedef enum {
	AUTOSTART_NORMAL,
	AUTOSTART_LOW,
	AUTOSTART_HIGH
} AutostartPriority;

typedef struct {
	WScreen *scr;
	char *command;
	char *processName;
	AutostartPriority priority;
} AutostartLaunch;

static WMPropList *getValue(WMPropList *item, const char *name)
{
	WMPropList *key = WMCreatePLString(name);
	WMPropList *value = WMGetFromPLDictionary(item, key);

	WMReleasePropList(key);
	return value && WMIsPLString(value) ? value : NULL;
}

static Bool getBool(WMPropList *item, const char *name, Bool fallback)
{
	WMPropList *value = getValue(item, name);
	const char *text = value ? WMGetFromPLString(value) : NULL;

	if (!text)
		return fallback;
	if (!strcasecmp(text, "yes") || !strcasecmp(text, "y") || !strcasecmp(text, "true") || !strcmp(text, "1"))
		return True;
	if (!strcasecmp(text, "no") || !strcasecmp(text, "n") || !strcasecmp(text, "false") || !strcmp(text, "0"))
		return False;
	return fallback;
}

static char *nextToken(const char **cursor)
{
	const char *p = *cursor;
	char *token, *out;
	char quote = 0;

	while (isspace((unsigned char)*p))
		p++;
	if (!*p || *p == ';' || *p == '&' || *p == '|') {
		*cursor = p;
		return NULL;
	}
	token = out = wmalloc(strlen(p) + 1);
	for (; *p; p++) {
		if (quote) {
			if (*p == quote)
				quote = 0;
			else if (*p == '\\' && quote == '"' && p[1])
				*out++ = *++p;
			else
				*out++ = *p;
		} else if (*p == '\'' || *p == '"') {
			quote = *p;
		} else if (*p == '\\' && p[1]) {
			*out++ = *++p;
		} else if (isspace((unsigned char)*p) || *p == ';' || *p == '&' || *p == '|') {
			break;
		} else {
			*out++ = *p;
		}
	}
	*out = 0;
	*cursor = p;
	return token;
}

static Bool isAssignment(const char *token)
{
	const char *p = token;

	if (!isalpha((unsigned char)*p) && *p != '_')
		return False;
	while (isalnum((unsigned char)*p) || *p == '_')
		p++;
	return *p == '=';
}

static Bool isInterpreter(const char *name)
{
	static const char *const interpreters[] = {
		"sh", "bash", "dash", "zsh", "ksh", "fish", "perl", "ruby", "node", "lua", "python", NULL
	};
	int i;

	if (!strncmp(name, "python", 6))
		return True;
	for (i = 0; interpreters[i]; i++)
		if (!strcmp(name, interpreters[i]))
			return True;
	return False;
}

char *wAutostartProcessName(const char *command)
{
	const char *cursor = command;
	char *token, *name;
	Bool wrapper = False, interpreter = False;

	while ((token = nextToken(&cursor))) {
		if (interpreter && token[0] == '-') {
			Bool inlineCode = !strcmp(token, "-c");

			wfree(token);
			if (inlineCode && (token = nextToken(&cursor))) {
				name = wAutostartProcessName(token);
				wfree(token);
				return name;
			}
			continue;
		}
		if (isAssignment(token) || !strcmp(token, "exec") || !strcmp(token, "nohup") ||
		    !strcmp(token, "env") || !strcmp(token, "nice")) {
			wrapper = !strcmp(token, "env") || !strcmp(token, "nice");
			wfree(token);
			continue;
		}
		if (wrapper && token[0] == '-') {
			Bool hasValue = !strcmp(token, "-n") || !strcmp(token, "-u");

			wfree(token);
			if (hasValue && (token = nextToken(&cursor)))
				wfree(token);
			continue;
		}
		name = strrchr(token, '/');
		name = wstrdup(name ? name + 1 : token);
		wfree(token);
		if (!interpreter && isInterpreter(name)) {
			/* Without a script argument the interpreter itself is the process. */
			const char *rest = cursor;
			char *script = NULL;

			while ((token = nextToken(&rest))) {
				if (token[0] != '-' || !strcmp(token, "-c")) {
					script = token;
					break;
				}
				wfree(token);
			}
			if (script) {
				wfree(script);
				wfree(name);
				interpreter = True;
				wrapper = False;
				continue;
			}
		}
		if (*name)
			return name;
		wfree(name);
		return NULL;
	}
	return NULL;
}

Bool wAutostartCmdlineMatches(const char *cmdline, size_t length, const char *name)
{
	size_t nameLength = strlen(name), i;

	if (!nameLength || length < nameLength)
		return False;
	for (i = 0; i + nameLength <= length; i++) {
		char after;

		if (i > 0 && cmdline[i - 1] != '/')
			continue;
		if (memcmp(cmdline + i, name, nameLength))
			continue;
		after = i + nameLength < length ? cmdline[i + nameLength] : 0;
		if (after == 0 || isspace((unsigned char)after))
			return True;
	}
	return False;
}

/* Returns 1 if running, 0 if not, -1 if the process table is unavailable. */
static int processRunning(const char *name)
{
	DIR *dir = opendir("/proc");
	struct dirent *entry;
	uid_t uid = getuid();
	pid_t self = getpid();
	int found = 0;

	if (!dir)
		return -1;
	while (!found && (entry = readdir(dir))) {
		char path[64], cmdline[4096];
		struct stat st;
		char *end;
		long pid = strtol(entry->d_name, &end, 10);
		ssize_t length;
		int fd;

		if (*end || pid <= 0 || pid == self)
			continue;
		snprintf(path, sizeof(path), "/proc/%ld", pid);
		if (stat(path, &st) < 0 || st.st_uid != uid)
			continue;
		snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		length = read(fd, cmdline, sizeof(cmdline));
		close(fd);
		if (length <= 0)
			continue;
		/* Arguments are NUL separated; treat separators as spaces like pgrep -f. */
		for (end = cmdline; end < cmdline + length - 1; end++)
			if (*end == 0)
				*end = ' ';
		if (cmdline[length - 1] == 0)
			length--;
		found = wAutostartCmdlineMatches(cmdline, length, name);
	}
	closedir(dir);
	return found;
}

static void setPriority(AutostartPriority priority)
{
	int current, target;

	if (priority == AUTOSTART_NORMAL)
		return;
	errno = 0;
	current = getpriority(PRIO_PROCESS, 0);
	if (errno)
		current = 0;
	if (priority == AUTOSTART_LOW)
		target = WMAX(current, LOW_PRIORITY_NICE);
	else
		target = current - HIGH_PRIORITY_STEP;
	if (target != current && setpriority(PRIO_PROCESS, 0, target) < 0)
		wwarning(_("could not change the priority of autostart command: %s"), strerror(errno));
}

static void launch(void *data)
{
	AutostartLaunch *item = data;
	pid_t pid;

	if (item->processName) {
		int running = processRunning(item->processName);

		if (running != 0)
			goto done;
	}
	pid = fork();
	if (pid == 0) {
		SetupEnvironment(item->scr);
#ifdef HAVE_SETSID
		setsid();
#endif
		setPriority(item->priority);
		execl("/bin/sh", "/bin/sh", "-c", item->command, NULL);
		werror("could not execute /bin/sh -c %s", item->command);
		_exit(127);
	} else if (pid < 0) {
		werror(_("cannot fork a new process"));
	}

done:
	wfree(item->command);
	if (item->processName)
		wfree(item->processName);
	wfree(item);
}

void wAutostartRun(WScreen *scr, WMPropList *dict, Bool restarting)
{
	WMPropList *key, *array;
	int i;

	if (!dict)
		return;
	key = WMCreatePLString("AutostartCommands");
	array = WMGetFromPLDictionary(dict, key);
	WMReleasePropList(key);
	if (!array)
		return;
	if (!WMIsPLArray(array)) {
		wwarning(_("Wrong option format for key \"%s\". Should be %s."), "AutostartCommands", "array");
		return;
	}

	for (i = 0; i < WMGetPropListItemCount(array); i++) {
		WMPropList *entry = WMGetFromPLArray(array, i), *value;
		AutostartLaunch *item;
		const char *text;
		long delay = 0;
		char *command;

		if (!WMIsPLDictionary(entry) || !(value = getValue(entry, "Command"))) {
			wwarning(_("Invalid entry in AutostartCommands at index %d"), i);
			continue;
		}
		command = wtrimspace(WMGetFromPLString(value));
		if (!*command || !getBool(entry, "Enabled", True)) {
			wfree(command);
			continue;
		}

		item = wmalloc(sizeof(*item));
		item->scr = scr;
		item->command = command;
		if (restarting && getBool(entry, "NoRelaunch", False))
			item->processName = wAutostartProcessName(command);
		text = (value = getValue(entry, "Priority")) ? WMGetFromPLString(value) : "";
		if (!strcasecmp(text, "Low"))
			item->priority = AUTOSTART_LOW;
		else if (!strcasecmp(text, "High"))
			item->priority = AUTOSTART_HIGH;
		else
			item->priority = AUTOSTART_NORMAL;
		if ((value = getValue(entry, "Delay"))) {
			delay = strtol(WMGetFromPLString(value), NULL, 10);
			delay = WMAX(0, WMIN(delay, MAX_AUTOSTART_DELAY));
		}

		if (delay > 0)
			WMAddTimerHandler(delay * 1000, launch, item);
		else
			launch(item);
	}
}
