#ifndef WM_MENU_GENERATION_CACHE_H
#define WM_MENU_GENERATION_CACHE_H

#include <glib.h>

typedef struct WMApplicationsCache WMApplicationsCache;

WMApplicationsCache *wApplicationsCacheCreate(const char *program, const char *category,
					      const char *theme, int size, gboolean noIcons);
char *wApplicationsCacheLoad(WMApplicationsCache *cache);
void wApplicationsCachePrepare(WMApplicationsCache *cache);
void wApplicationsCacheWatchPath(WMApplicationsCache *cache, const char *path);
void wApplicationsCacheSave(WMApplicationsCache *cache, const char *menu);
void wApplicationsCacheDestroy(WMApplicationsCache *cache);

#endif
