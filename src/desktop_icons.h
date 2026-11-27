#ifndef WM_DESKTOP_ICONS_H
#define WM_DESKTOP_ICONS_H

#include "wconfig.h"

#ifdef HAVE_GIO
#include <gio/gio.h>

typedef struct WMDesktopIcons WMDesktopIcons;
typedef void (*WMDesktopIconWatch)(void *data, const char *path);

gboolean wDesktopIconThemeValid(const char *name);
char *wDesktopIconTheme(void);
WMDesktopIcons *wDesktopIconsCreate(const char *theme, int size, WMDesktopIconWatch watch, void *data);
const char *wDesktopIconsResolve(WMDesktopIcons *icons, GIcon *icon);
void wDesktopIconsDestroy(WMDesktopIcons *icons);

#endif
#endif
