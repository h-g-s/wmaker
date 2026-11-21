#ifndef WM_APPLICATIONS_MENU_H
#define WM_APPLICATIONS_MENU_H

#include <string.h>

typedef struct {
	const char *id;
	const char *title;
} WMApplicationsCategory;

static const WMApplicationsCategory wApplicationsCategories[] = {
	{ "", N_("All Applications") },
	{ "AudioVideo", N_("Multimedia") },
	{ "Development", N_("Development") },
	{ "Education", N_("Education") },
	{ "Game", N_("Games") },
	{ "Graphics", N_("Graphics") },
	{ "Network", N_("Network") },
	{ "Office", N_("Office") },
	{ "Science", N_("Science") },
	{ "Settings", N_("Settings") },
	{ "System", N_("System") },
	{ "Utility", N_("Utilities") },
	{ "TerminalEmulator", N_("Terminals") },
	{ "WebBrowser", N_("Web Browsers") },
	{ "FileManager", N_("File Managers") },
	{ "Other", N_("Other") }
};

#define WM_APPLICATION_CATEGORY_COUNT \
	(sizeof(wApplicationsCategories) / sizeof(wApplicationsCategories[0]))
#define WM_APPLICATION_MAIN_CATEGORY_COUNT 11

static inline int wApplicationsCategoryValid(const char *category)
{
	const unsigned char *p = (const unsigned char *)category;

	if (!p || !*p)
		return 0;
	for (; *p; p++)
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
		      (*p >= '0' && *p <= '9') || *p == '-'))
			return 0;
	return 1;
}

static inline const char *wApplicationsCategoryTitle(const char *category)
{
	unsigned int i;

	for (i = 0; i < WM_APPLICATION_CATEGORY_COUNT; i++)
		if (strcmp(category, wApplicationsCategories[i].id) == 0)
			return wApplicationsCategories[i].title;
	return category;
}

#endif
