#include "wconfig.h"

#ifdef HAVE_GIO
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <WINGs/WUtil.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop_entry.h"

static void check(const char *id, const char *instance, const char *class_name, const char *expected)
{
	char *command = wDesktopEntryCommand(id, instance, class_name);

	if (g_strcmp0(command, expected) != 0)
		fprintf(stderr, "match %s/%s/%s: got %s, expected %s\n",
			id ? id : "-", instance ? instance : "-", class_name ? class_name : "-",
			command ? command : "-", expected ? expected : "-");
	if (expected)
		assert(command && strcmp(command, expected) == 0);
	else
		assert(!command);
	wfree(command);
}

static void entry(const char *dir, const char *id, const char *extra)
{
	char *path = g_build_filename(dir, id, NULL);
	char *contents = g_strdup_printf("[Desktop Entry]\nType=Application\nName=Test\nExec=/bin/true\n%s", extra);
	GError *error = NULL;

	assert(g_file_set_contents(path, contents, -1, &error));
	g_free(contents);
	g_free(path);
}

int main(void)
{
	GError *error = NULL;
	char *root = g_dir_make_tmp("wm-desktop-entry-XXXXXX", &error);
	char *apps;

	assert(root);
	g_setenv("XDG_DATA_HOME", root, TRUE);
	g_setenv("XDG_DATA_DIRS", root, TRUE);
	apps = g_build_filename(root, "applications", NULL);
	assert(g_mkdir_with_parents(apps, 0700) == 0);
	entry(apps, "net.thunderbird.Thunderbird.desktop", "StartupWMClass=Thunderbird\n");
	entry(apps, "org.example.Editor.desktop", "StartupWMClass=EditorWindow\nNoDisplay=true\n");
	entry(apps, "one.desktop", "StartupWMClass=Ambiguous\n");
	entry(apps, "two.desktop", "StartupWMClass=Ambiguous\n");
	entry(apps, "collision.desktop", "");
	entry(apps, "alias.desktop", "StartupWMClass=collision\n");
	entry(apps, "quo'te%test.desktop", "StartupWMClass=Quoted\n");
	entry(apps, "hidden.desktop", "Hidden=true\nStartupWMClass=HiddenWindow\n");

	check("net.thunderbird.Thunderbird", NULL, "Other",
	      "wmmenugen --launch 'net.thunderbird.Thunderbird.desktop'");
	check(NULL, NULL, "net.thunderbird.Thunderbird",
	      "wmmenugen --launch 'net.thunderbird.Thunderbird.desktop'");
	check(NULL, NULL, "EditorWindow", "wmmenugen --launch 'org.example.Editor.desktop'");
	check(NULL, NULL, "Ambiguous", NULL);
	check(NULL, NULL, "collision", NULL);
	check("collision", NULL, "collision", "wmmenugen --launch 'collision.desktop'");
	check(NULL, NULL, "HiddenWindow", NULL);
	check(NULL, NULL, "missing", NULL);
	check(NULL, NULL, "Quoted", "wmmenugen --launch 'quo\\\\'te\\%test.desktop'");
	assert(wDesktopCommandExecutable("/bin/true --version"));
	assert(!wDesktopCommandExecutable("wm-test-command-that-does-not-exist"));
	const char *files[] = { "net.thunderbird.Thunderbird.desktop", "org.example.Editor.desktop",
				"one.desktop", "two.desktop", "collision.desktop", "alias.desktop",
				"quo'te%test.desktop", "hidden.desktop" };
	for (unsigned int i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		char *path = g_build_filename(apps, files[i], NULL);
		assert(g_remove(path) == 0);
		g_free(path);
	}
	assert(g_rmdir(apps) == 0);
	assert(g_rmdir(root) == 0);
	g_free(apps);
	g_free(root);
	return 0;
}
#else
int main(void)
{
	return 0;
}
#endif
