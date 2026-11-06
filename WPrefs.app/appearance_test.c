#include "WPrefs.h"
#undef NDEBUG
#include <assert.h>
#include "Appearance.c"
#define colorWellObserver textureColorWellObserver
#include "TexturePanel.c"
#undef colorWellObserver

static WMPropList *domain;

WMPropList *GetObjectForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMGetFromPLDictionary(domain, name);

	WMReleasePropList(name);
	return value;
}

char *GetStringForKey(const char *key)
{
	WMPropList *value = GetObjectForKey(key);

	return value ? WMGetFromPLString(value) : NULL;
}

void SetObjectForKey(WMPropList *value, const char *key)
{
	WMPropList *name = WMCreatePLString(key);

	WMPutInPLDictionary(domain, name, value);
	WMReleasePropList(name);
}

void SetStringForKey(const char *value, const char *key)
{
	WMPropList *text = WMCreatePLString(value);

	SetObjectForKey(text, key);
	WMReleasePropList(text);
}

void AddSection(Panel *panel, const char *icon)
{
	(void)panel;
	(void)icon;
}

char *LocateImage(const char *name)
{
	char *path = wmalloc(strlen(TEST_IMAGE_PATH) + strlen(name) + 7);

	sprintf(path, "%s/%s.tiff", TEST_IMAGE_PATH, name);
	return path;
}

static void bounds(WMView *view)
{
	if (view->self && WMWidgetClass(view->self) == WC_List)
		return;
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		if (child->pos.x < 0 || child->pos.y < 0 ||
		    child->pos.x + child->size.width > view->size.width ||
		    child->pos.y + child->size.height > view->size.height)
			fprintf(stderr, "Overflow %u in %u: %d,%d %ux%u in %ux%u\n",
				child->self ? WMWidgetClass(child->self) : 0,
				view->self ? WMWidgetClass(view->self) : 0,
				child->pos.x, child->pos.y, child->size.width, child->size.height,
				view->size.width, view->size.height);
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		bounds(child);
	}
}

static void clickPreview(_Panel *panel, WMRect rect)
{
	XEvent event = { .xbutton.type = ButtonPress };

	event.xbutton.x = ((int)WMWidgetWidth(panel->prevL) - previewScale(panel, 236)) / 2
		+ rect.pos.x + rect.size.width / 2;
	event.xbutton.y = ((int)WMWidgetHeight(panel->prevL) - previewScale(panel, 226)) / 2
		+ rect.pos.y + rect.size.height / 2;
	previewClick(&event, panel);
}

static void destroyPanel(_Panel *panel)
{
	Display *display = WMScreenDisplay(WMWidgetScreen(panel->box));

	WMRemoveNotificationObserver(panel);
	if (panel->texturePanel) {
		WMRemoveNotificationObserver(panel->texturePanel);
		WMDestroyWidget(panel->texturePanel->win);
		wfree(panel->texturePanel);
	}
	for (int i = 0; i < WMGetListNumberOfRows(panel->texLs); i++) {
		TextureListItem *item = WMGetListItem(panel->texLs, i)->clientData;

		XFreePixmap(display, item->preview);
		WMReleasePropList(item->prop);
		wfree(item->title);
		wfree(item->texture);
		if (item->path) {
			assert(remove(item->path) == 0);
			wfree(item->path);
		}
		wfree(item);
	}
	for (int i = 0; i < wlengthof(colorOptions); i++)
		WMReleaseColor(panel->colors[i]);
	XFreePixmap(display, panel->previewNoText);
	XFreePixmap(display, panel->previewBack);
	WMReleasePixmap(panel->onLed);
	WMReleasePixmap(panel->offLed);
	WMReleasePixmap(panel->hand);
	WMReleaseFont(panel->smallFont);
	WMReleaseFont(panel->normalFont);
	WMReleaseFont(panel->boldFont);
	if (panel->fprefix)
		wfree(panel->fprefix);
	WMDestroyWidget(panel->parent);
	wfree(panel);
}

int main(int argc, char **argv)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	char root[] = "/tmp/wmaker-appearance-test-XXXXXX";
	char *library, *cache, *directory, *defaults, *settings;
	Display *display;
	WMScreen *screen;

	if (!getenv("DISPLAY")) {
		puts("SKIP: appearance_test needs an X server");
		return 77;
	}
	assert(mkdtemp(root));
	assert(setenv("WMAKER_USER_ROOT", root, 1) == 0);
	defaults = wstrconcat(root, "/Defaults");
	settings = wstrconcat(defaults, "/appearance_test");
	assert(mkdir(defaults, 0755) == 0);
	library = wstrconcat(root, "/Library");
	assert(mkdir(library, 0755) == 0);
	directory = wstrconcat(library, "/" PACKAGE_TARNAME);
	cache = wstrconcat(directory, "/WPrefs");
	WMInitializeApplication("appearance_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);

	for (int n = 0; n < wlengthof(sizes); n++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[n]);
		WMWindow *window;
		_Panel *panel;
		WMSize size;

		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		font = WMBoldSystemFontOfSize(screen, sizes[n]);
		WMReleaseFont(screen->boldFont);
		screen->boldFont = font;
		domain = WMCreatePLDictionary(NULL, NULL);
		SetStringForKey("center", "TitleJustify");
		SetStringForKey("normal", "MenuStyle");
		window = WMCreateWindow(screen, "appearanceTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitAppearance(window);
		assert(panel->callbacks.automaticLayout);
		createPanel(panel);
		XImage *mask = XGetImage(display, WMGetPixmapMaskXID(panel->hand), 0, 0, 1, 1, AllPlanes, ZPixmap);
		assert(mask && XGetPixel(mask, 0, 0) == 0);
		XDestroyImage(mask);
		assert(WMFontHeight(panel->boldFont) <= panel->textureRects[PFOCUSED].size.height);
		assert(WMWidthOfString(panel->boldFont, _("Owner of Focused Window"),
			strlen(_("Owner of Focused Window"))) + 10 <= panel->textureRects[POWNER].size.width);
		assert(!panel->texturePanel && !panel->fprefix && !panel->loaded);
		prepareForClose(panel);
		assert(!WMGetUDObjectForKey(WMGetStandardUserDefaults(), "ColorList"));
		assert(access(directory, F_OK) != 0);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMSize previousSwatch = { 0, 0 };
		for (int enlarged = 0; enlarged < 2; enlarged++) {
			WMResizeWidget(window, size.width + 4 + enlarged * 200, size.height + 4 + enlarged * 200);
			for (int tab = 0; tab < 3; tab++) {
				WMSelectTabViewItemAtIndex(panel->tabv, tab);
				bounds(WMWidgetView(panel->box));
			}
			WMSelectTabViewItemAtIndex(panel->tabv, TAB_TEXTURE);
			for (int i = 0; i < PBACKGROUND; i++) {
				clickPreview(panel, panel->textureRects[i]);
				assert(WMGetPopUpButtonSelectedItem(panel->secP) == i);
			}
			WMSelectTabViewItemAtIndex(panel->tabv, TAB_COLOR);
			WMSize swatch = WMGetViewSize(WMWidgetView(panel->sampW[0]));
			assert(swatch.width > previousSwatch.width && swatch.height > previousSwatch.height);
			previousSwatch = swatch;
			for (int i = 0; i < wlengthof(sample_colors); i++) {
				WMView *well = WMWidgetView(panel->sampW[i]);
				WMView *row = well->parent;
				WMView *grid = row->parent;

				assert(well->size.width == swatch.width && well->size.height == swatch.height);
				if (i % 4 == 3)
					assert(row->size.width - well->pos.x - well->size.width < 4);
				if (i == wlengthof(sample_colors) - 1)
					assert(grid->size.height - row->pos.y - row->size.height < 6);
			}
			for (int i = 0; i < wlengthof(colorOptions); i++) {
				if (!panel->colorRects[i].size.width)
					continue;
				clickPreview(panel, panel->colorRects[i]);
				int expected = i == MHIGHT_COL ? MITEM_COL : i >= ICONT_COL ? ICONT_COL : i;
				assert(WMGetPopUpButtonSelectedItem(panel->colP) == expected);
			}
		}
		showPanel(panel);
		WMColor *changed = WMCreateNamedColor(screen, "#123456", False);
		WMSetPopUpButtonSelectedItem(panel->colP, FTITLE_COL);
		changeColorPage(panel->colP, panel);
		WMSetColorWellColor(panel->colW, changed);
		colorWellObserver(panel, NULL);
		WMReleaseColor(changed);
		storeData(panel);
		changed = WMCreateNamedColor(screen, GetStringForKey("FTitleColor"), False);
		assert(changed && WMRedComponentOfColor(changed) >> 8 == 0x12);
		WMReleaseColor(changed);
		for (int i = 0; i < wlengthof(menu_style); i++) {
			WMPerformButtonClick(panel->mstyB[i]);
			storeData(panel);
			assert(strcmp(GetStringForKey("MenuStyle"), menu_style[i].db_value) == 0);
		}
		for (int i = 0; i < wlengthof(wintitle_align); i++) {
			WMPerformButtonClick(panel->taliB[i]);
			storeData(panel);
			assert(strcmp(GetStringForKey("TitleJustify"), wintitle_align[i].db_value) == 0);
		}
		WMSelectTabViewItemAtIndex(panel->tabv, TAB_TEXTURE);
		WMSetPopUpButtonSelectedItem(panel->secP, PFOCUSED);
		changePage(panel->secP, panel);
		editTexture(NULL, panel);
		assert(panel->texturePanel && !panel->fprefix);
		WMPropList *edited = WMCreatePropListFromDescription("(solid, red)");
		SetTexturePanelTexture(panel->texturePanel, "Edited", edited);
		WMReleasePropList(edited);
		okEditTexture(panel);
		storeData(panel);
		WMColor *red = WMCreateNamedColor(screen,
			WMGetFromPLString(WMGetFromPLArray(GetObjectForKey("FTitleBack"), 1)), False);
		assert(red && WMRedComponentOfColor(red) == 65535 && WMGreenComponentOfColor(red) == 0);
		WMReleaseColor(red);
		newTexture(NULL, panel);
		okNewTexture(panel);
		assert(WMGetListNumberOfRows(panel->texLs) == 9);
		assert(panel->fprefix && access(cache, F_OK) == 0);
		WMSelectListItem(panel->texLs, 8);
		textureClick(panel->texLs, panel);
		assert(WMGetButtonEnabled(panel->delB));
		textureDoubleClick(panel->texLs, panel);
		storeData(panel);
		assert(panel->textureIndex[PFOCUSED] == 8);
		assert(WMIsPropListEqualTo(GetObjectForKey("FTitleBack"),
			((TextureListItem *)WMGetListItem(panel->texLs, 8)->clientData)->prop));
		if (n == wlengthof(sizes) - 1) {
			WMList *restored = WMCreateList(window);
			WMPropList *saved;
			TextureListItem *item;

			prepareForClose(panel);
			saved = WMGetUDObjectForKey(WMGetStandardUserDefaults(), "TextureList");
			assert(saved && WMGetPropListItemCount(saved) == 1);
			saved = WMGetUDObjectForKey(WMGetStandardUserDefaults(), "ColorList");
			assert(saved && WMGetPropListItemCount(saved) == wlengthof(sample_colors));
			assert(access(settings, F_OK) == 0);
			fillTextureList(restored);
			assert(WMGetListNumberOfRows(restored) == 1);
			item = WMGetListItem(restored, 0)->clientData;
			assert(item->preview && strcmp(item->title, "New Texture") == 0);
			assert(WMIsPropListEqualTo(item->prop, ((TextureListItem *)WMGetListItem(panel->texLs, 8)->clientData)->prop));
			XFreePixmap(display, item->preview);
			WMReleasePropList(item->prop);
			wfree(item->title);
			wfree(item->texture);
			wfree(item->path);
			wfree(item);
			WMDestroyWidget(restored);
		}
		destroyPanel(panel);
		WMReleasePropList(domain);
		assert(rmdir(cache) == 0);
		assert(rmdir(directory) == 0);
	}
	WMReleaseApplication();
	XCloseDisplay(display);
	assert(rmdir(library) == 0);
	assert(remove(settings) == 0);
	assert(rmdir(defaults) == 0);
	assert(rmdir(root) == 0);
	wfree(cache);
	wfree(directory);
	wfree(library);
	wfree(settings);
	wfree(defaults);
	puts("Font-aware appearance layout, preview picking and texture editing tests passed");
	return 0;
}
