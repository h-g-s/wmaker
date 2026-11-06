#ifndef WPREFS_FONT_CONTROLS_H
#define WPREFS_FONT_CONTROLS_H

#include <WINGs/WINGs.h>

/* Keep these records alive until their parent widgets are destroyed. */
typedef struct {
	WMBox *box;
	WMButton *buttons[5];
} WPrefsSpeedControl;

typedef struct {
	WMBox *box;
	WMButton *buttons[8];
	WMFrame *screen;
	WMFrame *marker;
	int position;
	int unit;
	int buttonSize;
	int gap;
} WPrefsIconPositionControl;

void WPrefsCreateSpeedControl(WPrefsSpeedControl *control, WMWidget *parent, WMFont *font);
void WPrefsSetSpeedControlFont(WPrefsSpeedControl *control, WMFont *font);
void WPrefsSetSpeedControlValue(WPrefsSpeedControl *control, int value);
int WPrefsGetSpeedControlValue(WPrefsSpeedControl *control);

/* Clock indices 0..5 match timer0..timer5 (0 is the empty stopwatch). */
void WPrefsSetClockButtonFont(WMButton *button, WMFont *font, int clock);

void WPrefsSetMenuAlignmentButtonFont(WMButton *button, WMFont *font, Bool aligned);

void WPrefsSetPreviewButtonFont(WMButton *button, WMFont *font, WMPixmap *normal, WMPixmap *selected);

void WPrefsCreateIconPositionControl(WPrefsIconPositionControl *control, WMWidget *parent, WMFont *font);
void WPrefsSetIconPositionControlFont(WPrefsIconPositionControl *control, WMFont *font);
void WPrefsSetIconPositionControlValue(WPrefsIconPositionControl *control, int value);

#endif
