#ifndef WM_APPLICATION_DEFAULTS_H
#define WM_APPLICATION_DEFAULTS_H

#include "../src/wconfig.h"

#ifdef HAVE_GIO
#include <gio/gdesktopappinfo.h>

typedef enum {
	WMDefaultBrowser,
	WMDefaultEmail,
	WMDefaultTerminal,
	WMDefaultFileManager,
	WMDefaultArchiveManager,
	WMDefaultRoleCount
} WMDefaultRole;

GPtrArray *WMDefaultApplications(WMDefaultRole role);
char *WMDefaultApplicationID(WMDefaultRole role, GError **error);
gboolean WMSetDefaultApplication(WMDefaultRole role, GAppInfo *app, GError **error);

#endif
#endif
