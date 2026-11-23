#ifndef WPREFS_MENU_PREFERENCES_H
#define WPREFS_MENU_PREFERENCES_H

#include "FontControls.h"

typedef struct {
	WMBox *box;
	WPrefsSpeedControl speed;
	WMButton *aliyB;
	WMButton *alinB;
	WMButton *autoB;
	WMButton *autoC;
	WMButton *wrapB;
} WPrefsMenuPreferences;

void WPrefsCreateMenuPreferences(WPrefsMenuPreferences *prefs, WMWidget *parent, WMFont *font);
void WPrefsStoreMenuPreferences(WPrefsMenuPreferences *prefs);

#endif
