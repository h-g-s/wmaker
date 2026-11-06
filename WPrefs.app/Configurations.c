/* Configurations.c- misc. configurations
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
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
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "WPrefs.h"
#include "PanelLayout.h"
#include "FontControls.h"

typedef struct _Panel {
	WMBox *box;
	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMFrame *icoF;
	WPrefsSpeedControl iconSpeed;

	WMFrame *shaF;
	WPrefsSpeedControl shadeSpeed;

	WMFrame *titlF;
	WMButton *oldsB;
	WMButton *newsB;
	WMButton *nextB;

	WMFrame *animF;
	WMButton *animB;
	WMButton *supB;
	WMLabel *noteL;

	WMFrame *smoF;
	WMButton *smoB;

	WMFrame *dithF;
	WMButton *dithB;
	WMSlider *dithS;
	WMLabel *dithL;
	WMLabel *dith1L;
	WMLabel *dith2L;

	int cmapSize;
} _Panel;

#define ICON_FILE	"configs"
#define OLDS_IMAGE	"oldstyle"
#define NEWS_IMAGE	"newstyle"
#define NEXT_IMAGE	"nextstyle"
#define ANIM_IMAGE	"animations"
#define SUPERF_IMAGE	"moreanim"
#define SMOOTH_IMAGE	"smooth"
#define ARQUIVO_XIS	"xis"

static void updateLabel(WMWidget *self, void *data);

static void showData(_Panel *panel)
{
	char *str;

	WPrefsSetSpeedControlValue(&panel->iconSpeed, GetSpeedForKey("IconSlideSpeed"));
	WPrefsSetSpeedControlValue(&panel->shadeSpeed, GetSpeedForKey("ShadeSpeed"));

	str = GetStringForKey("NewStyle");
	if (str && strcasecmp(str, "next") == 0) {
		WMPerformButtonClick(panel->nextB);
	} else if (str && strcasecmp(str, "old") == 0) {
		WMPerformButtonClick(panel->oldsB);
	} else {
		WMPerformButtonClick(panel->newsB);
	}

	WMSetButtonSelected(panel->animB, !GetBoolForKey("DisableAnimations"));
	WMSetButtonSelected(panel->supB, GetBoolForKey("Superfluous"));
	WMSetButtonSelected(panel->smoB, GetBoolForKey("SmoothWorkspaceBack"));
	WMSetButtonSelected(panel->dithB, GetBoolForKey("DisableDithering"));
	WMSetSliderValue(panel->dithS, GetIntegerForKey("ColormapSize"));
	updateLabel(panel->dithS, panel);
}

static void updateLabel(WMWidget *self, void *data)
{
	WMSlider *sPtr = (WMSlider *) self;
	_Panel *panel = (_Panel *) data;
	char buffer[64];
	float fl;

	fl = WMGetSliderValue(sPtr);
	panel->cmapSize = (int)fl;

	sprintf(buffer, "%i", panel->cmapSize * panel->cmapSize * panel->cmapSize);
	WMSetLabelText(panel->dithL, buffer);
}

static void setTitlebarImage(WMButton *button, WMFont *font, const char *name)
{
	char *path = LocateImage(name);
	WMPixmap *image = path ? WMCreatePixmapFromFile(WMWidgetScreen(button), path) : NULL;

	if (!image)
		wwarning(_("could not load titlebar preview %s"), name);
	WPrefsSetImageButtonFont(button, font, image, NULL);
	if (image)
		WMReleasePixmap(image);
	if (path)
		wfree(path);
}

static void setSmoothImages(WMButton *button, WMFont *font)
{
	WMScreen *screen = WMWidgetScreen(button);
	char *path = LocateImage(SMOOTH_IMAGE);
	RImage *image = path ? RLoadImage(WMScreenRContext(screen), path, 0) : NULL;
	RColor background = WMGetRColorFromColor(WMGetWidgetBackgroundColor(button));
	int side = 61 * WMAX(17, WMFontHeight(font)) / 17;

	if (path)
		wfree(path);
	if (!image) {
		wwarning(_("could not load smooth scaling preview: %s"), RMessageForError(RErrorCode));
		return;
	}
	RCombineImageWithColor(image, &background);
	for (int i = 0; i < 2; i++) {
		RImage *scaled = i ? RSmoothScaleImage(image, side, side) : RScaleImage(image, side, side);
		WMPixmap *icon = scaled ? WMCreatePixmapFromRImage(screen, scaled, 128) : NULL;

		if (scaled)
			RReleaseImage(scaled);
		if (icon) {
			if (i)
				WMSetButtonAltImage(button, icon);
			else
				WMSetButtonImage(button, icon);
			WMReleasePixmap(icon);
		} else {
			wwarning(_("could not scale background preview: %s"), RMessageForError(RErrorCode));
		}
	}
	RReleaseImage(image);
}

static void createPanel(Panel *p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMPixmap *icon, *altIcon;
	RImage *xis = NULL;
	RContext *rc = WMScreenRContext(scr);
	WMFont *font = WMDefaultSystemFont(scr);
	WMBox *columns, *left, *right, *row, *features, *colors;
	WPrefsGroup slide, shade, style, animations, smoothing, dithering;
	char *path;

	path = LocateImage(ARQUIVO_XIS);
	if (path) {
		xis = RLoadImage(rc, path, 0);
		if (!xis)
			wwarning(_("could not load image file %s"), path);
		wfree(path);
	}

	panel->box = WPrefsCreatePanelBox(panel->parent);
	columns = WMCreateBox(panel->box);
	WMSetBoxHorizontal(columns, True);
	left = WMCreateBox(columns);
	right = WMCreateBox(columns);

    /*********** Icon Slide Speed **********/
	slide = WPrefsCreateGroup(left, _("Icon Slide Speed"));
	panel->icoF = slide.frame;
	WPrefsCreateSpeedControl(&panel->iconSpeed, slide.content, font);
	WMPackBoxSubview(slide.content, panel->iconSpeed.box,
			 WMGetViewSize(WMWidgetView(panel->iconSpeed.box)), False, 0);
	WPrefsPackGroup(left, &slide, False, 12);

    /*********** Shade Animation Speed **********/
	shade = WPrefsCreateGroup(left, _("Shade Animation Speed"));
	panel->shaF = shade.frame;
	WPrefsCreateSpeedControl(&panel->shadeSpeed, shade.content, font);
	WMPackBoxSubview(shade.content, panel->shadeSpeed.box,
			 WMGetViewSize(WMWidgetView(panel->shadeSpeed.box)), False, 0);
	WPrefsPackGroup(left, &shade, False, 12);
	features = WMCreateBox(right);
	WMSetBoxHorizontal(features, True);

    /***************** Smoothed Scaling *****************/
	smoothing = WPrefsCreateGroup(features, _("Smooth Scaling"));
	panel->smoF = smoothing.frame;

	panel->smoB = WMCreateButton(smoothing.content, WBTToggle);
	WMSetButtonFont(panel->smoB, font);
	WMSetButtonImagePosition(panel->smoB, WIPImageOnly);
	setSmoothImages(panel->smoB, font);
	WMSetBalloonTextForView(_("Smooth scaled background images, neutralizing\n"
				  "the `pixelization' effect. This will slow\n"
				  "down loading of background images considerably."), WMWidgetView(panel->smoB));

	WPrefsPackButton(smoothing.content, panel->smoB, 0);

    /***************** Titlebar Style Size ****************/
	style = WPrefsCreateGroup(left, _("Titlebar Style"));
	panel->titlF = style.frame;
	row = WMCreateBox(style.content);
	WMSetBoxHorizontal(row, True);

	panel->oldsB = WMCreateButton(row, WBTOnOff);
	WMSetButtonImagePosition(panel->oldsB, WIPImageOnly);
	setTitlebarImage(panel->oldsB, font, OLDS_IMAGE);

	panel->newsB = WMCreateButton(row, WBTOnOff);
	WMSetButtonImagePosition(panel->newsB, WIPImageOnly);
	setTitlebarImage(panel->newsB, font, NEWS_IMAGE);

	panel->nextB = WMCreateButton(row, WBTOnOff);
	WMSetButtonImagePosition(panel->nextB, WIPImageOnly);
	setTitlebarImage(panel->nextB, font, NEXT_IMAGE);

	WMGroupButtons(panel->newsB, panel->oldsB);
	WMGroupButtons(panel->newsB, panel->nextB);

	WPrefsPackButton(row, panel->oldsB, 8);
	WPrefsPackButton(row, panel->newsB, 8);
	WPrefsPackButton(row, panel->nextB, 0);
	WMPackBoxSubview(style.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);
	WPrefsPackGroup(left, &style, True, 0);

    /**************** Features ******************/
	animations = WPrefsCreateGroup(features, _("Animations"));
	panel->animF = animations.frame;
	row = WMCreateBox(animations.content);
	WMSetBoxHorizontal(row, True);

	panel->animB = WMCreateButton(row, WBTToggle);
	WMSetButtonFont(panel->animB, font);
	WMSetButtonText(panel->animB, _("Animations"));
	WMSetButtonImagePosition(panel->animB, WIPAbove);
	CreateImages(scr, rc, xis, ANIM_IMAGE, &altIcon, &icon);
	WPrefsSetImageButtonFont(panel->animB, font, icon, altIcon);
	if (icon) {
		WMReleasePixmap(icon);
	}
	if (altIcon) {
		WMReleasePixmap(altIcon);
	}
	WMSetBalloonTextForView(_("Disable/enable animations such as those shown\n"
				  "for window miniaturization, shading etc."), WMWidgetView(panel->animB));

	panel->supB = WMCreateButton(row, WBTToggle);
	WMSetButtonFont(panel->supB, font);
	WMSetButtonText(panel->supB, _("Superfluous"));
	WMSetButtonImagePosition(panel->supB, WIPAbove);
	CreateImages(scr, rc, xis, SUPERF_IMAGE, &altIcon, &icon);
	WPrefsSetImageButtonFont(panel->supB, font, icon, altIcon);
	if (icon) {
		WMReleasePixmap(icon);
	}
	if (altIcon) {
		WMReleasePixmap(altIcon);
	}
	WMSetBalloonTextForView(_("Disable/enable `superfluous' features and\n"
				  "animations. These include the `ghosting' of the\n"
				  "dock when it's being moved to another side and\n"
				  "the explosion animation when undocking icons."), WMWidgetView(panel->supB));

	WPrefsPackButton(row, panel->animB, 8);
	WPrefsPackButton(row, panel->supB, 0);
	WMPackBoxSubview(animations.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(row);
	WPrefsPackGroup(features, &animations, True, 12);
	WPrefsPackGroup(features, &smoothing, True, 0);
	WMPackBoxSubview(right, features, WMGetBoxMinimumSize(features), False, 12);
	WMMapSubwidgets(features);

    /*********** Dithering **********/
	panel->cmapSize = 4;

	dithering = WPrefsCreateGroup(right, _("Dithering colormap for 8bpp"));
	panel->dithF = dithering.frame;

	WMSetBalloonTextForView(_("Number of colors to reserve for Window Maker\n"
				  "on displays that support only 8bpp (PseudoColor)."),
				WMWidgetView(panel->dithF));

	panel->dithB = WMCreateSwitchButton(dithering.content);
	WMSetButtonText(panel->dithB, _("Disable dithering in any visual/depth"));

	WPrefsPackButton(dithering.content, panel->dithB, 8);
	row = WMCreateBox(dithering.content);
	WMSetBoxHorizontal(row, True);
	colors = WMCreateBox(row);
	panel->dithL = WMCreateLabel(colors);
	WMSetLabelTextAlignment(panel->dithL, WACenter);
	WMSetLabelText(panel->dithL, "216");
	WPrefsPackLabel(colors, panel->dithL, 4);

	panel->dithS = WMCreateSlider(colors);
	WMSetSliderFont(panel->dithS, font);
	WMSetSliderMinValue(panel->dithS, 2);
	WMSetSliderMaxValue(panel->dithS, 6);
	WMSetSliderContinuous(panel->dithS, True);
	WMSetSliderAction(panel->dithS, updateLabel, panel);
	WMPackBoxSubview(colors, panel->dithS, WMGetSliderPreferredSize(panel->dithS), False, 0);

	panel->dith1L = WMCreateLabel(row);
	WMSetLabelTextAlignment(panel->dith1L, WACenter);
	WMSetLabelFont(panel->dith1L, font);
	WMSetLabelText(panel->dith1L, _("More colors for\napplications"));

	panel->dith2L = WMCreateLabel(row);
	WMSetLabelTextAlignment(panel->dith2L, WACenter);
	WMSetLabelFont(panel->dith2L, font);
	WMSetLabelText(panel->dith2L, _("More colors for\nWindow Maker"));

	WPrefsPackLabel(row, panel->dith1L, 8);
	WMPackBoxSubview(row, colors, WMGetBoxMinimumSize(colors), True, 8);
	WPrefsPackLabel(row, panel->dith2L, 0);
	WMPackBoxSubview(dithering.content, row, WMGetBoxMinimumSize(row), False, 0);
	WMMapSubwidgets(colors);
	WMMapSubwidgets(row);
	WPrefsPackGroup(right, &dithering, True, 0);
	WMPackBoxSubview(columns, left, WMGetBoxMinimumSize(left), True, 12);
	WMPackBoxSubview(columns, right, WMGetBoxMinimumSize(right), True, 0);
	WMPackBoxSubview(panel->box, columns, WMGetBoxMinimumSize(columns), True, 0);
	WMMapSubwidgets(left);
	WMMapSubwidgets(right);
	WMMapSubwidgets(columns);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	if (xis)
		RReleaseImage(xis);
	WMReleaseFont(font);
	showData(panel);
}

static void storeData(_Panel *panel)
{
	SetSpeedForKey(WPrefsGetSpeedControlValue(&panel->iconSpeed), "IconSlideSpeed");
	SetSpeedForKey(WPrefsGetSpeedControlValue(&panel->shadeSpeed), "ShadeSpeed");

	if (WMGetButtonSelected(panel->newsB)) {
		SetStringForKey("new", "NewStyle");
	} else if (WMGetButtonSelected(panel->oldsB)) {
		SetStringForKey("old", "NewStyle");
	} else {
		SetStringForKey("next", "NewStyle");
	}
	SetBoolForKey(!WMGetButtonSelected(panel->animB), "DisableAnimations");
	SetBoolForKey(WMGetButtonSelected(panel->supB), "Superfluous");
	SetBoolForKey(WMGetButtonSelected(panel->smoB), "SmoothWorkspaceBack");
	SetBoolForKey(WMGetButtonSelected(panel->dithB), "DisableDithering");
	SetIntegerForKey(WMGetSliderValue(panel->dithS), "ColormapSize");
}

Panel *InitConfigurations(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Other Configurations");
	panel->description = _("Animation speeds, titlebar styles, various option\n"
			       "toggling and number of colors to reserve for\n" "Window Maker on 8bit displays.");

	panel->parent = parent;
	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.automaticLayout = True;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
