#include "autostart.c"

#include <assert.h>

void SetupEnvironment(WScreen *scr)
{
	(void)scr;
}

static void checkName(const char *command, const char *expected)
{
	char *name = wAutostartProcessName(command);

	if (!expected) {
		assert(name == NULL);
		return;
	}
	assert(name != NULL);
	if (strcmp(name, expected)) {
		fprintf(stderr, "%s: got %s, expected %s\n", command, name, expected);
		assert(0);
	}
	wfree(name);
}

static Bool matches(const char *cmdline, const char *name)
{
	return wAutostartCmdlineMatches(cmdline, strlen(cmdline), name);
}

int main(void)
{
	checkName("picom --backend glx --vsync &", "picom");
	checkName("  /usr/bin/ulauncher --hide-window", "ulauncher");
	checkName("FOO=1 env -u BAR nice -n 5 nohup exec dnfdragora-updater", "dnfdragora-updater");
	checkName("'/opt/My App/tray app' --quiet", "tray app");
	checkName("sh -c 'xscreensaver -no-splash'", "xscreensaver");
	checkName("/bin/sh -x ~/bin/start-tray.sh", "start-tray.sh");
	checkName("python3 /usr/bin/dnfdragora-updater", "dnfdragora-updater");
	checkName("bash", "bash");
	checkName("   ", NULL);

	assert(matches("/usr/bin/python3 /usr/bin/dnfdragora-updater", "dnfdragora-updater"));
	assert(matches("picom --backend glx", "picom"));
	assert(matches("picom", "picom"));
	assert(!matches("picom-trans -o 50", "picom"));
	assert(!matches("vim picom.conf", "picom"));
	assert(!matches("xpicom", "picom"));
	assert(!matches("", "picom"));
	return 0;
}
