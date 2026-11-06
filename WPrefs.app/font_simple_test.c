#include "WPrefs.h"
#undef NDEBUG
#include <assert.h>
#include "FontSimple.c"

static WMPropList *domain;

char *GetStringForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMGetFromPLDictionary(domain, name);

	WMReleasePropList(name);
	return value ? WMGetFromPLString(value) : NULL;
}

void SetStringForKey(const char *text, const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMCreatePLString(text);

	WMPutInPLDictionary(domain, name, value);
	WMReleasePropList(name);
	WMReleasePropList(value);
}

void AddSection(Panel *panel, const char *icon)
{
	(void)panel;
	(void)icon;
}

static void bounds(WMView *view)
{
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		bounds(child);
	}
}

int main(int argc, char **argv)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	Display *display;
	WMScreen *screen;
	char root[] = "/tmp/wmaker-font-test-XXXXXX";
	const char *original = "sans serif:pixelsize=17.5:antialias=false";

	if (!getenv("DISPLAY"))
		return 77;
	assert(mkdtemp(root));
	assert(setenv("WMAKER_USER_ROOT", root, 1) == 0);
	WMInitializeApplication("font_simple_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	domain = WMCreatePLDictionary(NULL, NULL);
	for (int n = 0; n < 3; n++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[n]);
		WMWindow *window;
		_Panel *panel;
		WMSize size;
		WMMenuItem *option;

		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		for (int i = 0; i < 6; i++)
			SetStringForKey(original, fontOptions[i].option);
		SetStringForKey("serif:pixelsize=96", "LargeDisplayFont");
		window = WMCreateWindow(screen, "fontTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitFontSimple(window);
		createPanel((Panel *)panel);
		assert(W_VIEW(panel->box)->flags.realized);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		bounds(WMWidgetView(panel->box));
		assert(WMGetListNumberOfRows(panel->familyL) >= 2);
		WMSetTextFieldText(panel->sampleT, "Custom preview text");
		for (int i = 0; i < 6; i++) {
			WMSetPopUpButtonSelectedItem(panel->optionP, i);
			selectedOption(NULL, panel);
			option = WMGetPopUpButtonMenuItem(panel->optionP, i);
			assert(strcmp(WMGetMenuItemRepresentedObject(option),
				      i == 5 ? "serif:pixelsize=96" : original) == 0);
			assert(WMFontHeight(WMGetTextFieldFont(panel->sampleT)) + 8 <= WMWidgetHeight(panel->sampleT));
			{
				char *text = WMGetTextFieldText(panel->sampleT);

				assert(strcmp(text, "Custom preview text") == 0);
				wfree(text);
			}
		}
		WMSetPopUpButtonSelectedItem(panel->optionP, 0);
		selectedOption(NULL, panel);
		for (int i = 0; i < WMGetListNumberOfRows(panel->sizeL); i++) {
			FcPattern *pattern;
			double pixels;
			char *name;

			WMSelectListItem(panel->sizeL, i);
			selected(NULL, panel);
			name = WMGetMenuItemRepresentedObject(WMGetPopUpButtonMenuItem(panel->optionP, 0));
			pattern = FcNameParse((FcChar8 *)name);
			assert(FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &pixels) == FcResultMatch);
			assert(pixels == atoi(standardSizes[i]));
			FcPatternDestroy(pattern);
			assert(WMFontHeight(WMGetTextFieldFont(panel->sampleT)) + 8 <= WMWidgetHeight(panel->sampleT));
			assert(strstr(name, "antialias=False"));
		}
		WMSelectListItem(panel->familyL, panel->fonts->familyn - 1);
		selectedFamily(NULL, panel);
		assert(WMGetListNumberOfRows(panel->styleL) == 2);
		WMSelectListItem(panel->styleL, 1);
		selected(NULL, panel);
		storeData(panel);
		assert(strstr(GetStringForKey("WindowTitleFont"), "serif"));
		assert(strcmp(GetStringForKey("MenuTitleFont"), original) == 0);
		{
			char *path = wdefaultspathfordomain("WMGLOBAL");
			WMPropList *saved = WMReadPropListFromFile(path);

			assert(saved);
			for (int i = 6; i < wlengthof(fontOptions); i++) {
				WMPropList *key = WMCreatePLString(fontOptions[i].option);
				WMPropList *value = WMGetFromPLDictionary(saved, key);
				char *name = WMGetMenuItemRepresentedObject(WMGetPopUpButtonMenuItem(panel->optionP, i));

				assert(value && strcmp(WMGetFromPLString(value), name) == 0);
				WMReleasePropList(key);
			}
			WMReleasePropList(saved);
			wfree(path);
		}
		for (int i = 0; i < wlengthof(fontOptions); i++) {
			WMSetPopUpButtonSelectedItem(panel->optionP, i);
			selectedOption(NULL, panel);
			option = WMGetPopUpButtonMenuItem(panel->optionP, i);
			if (i < 6)
				assert(strcmp(WMGetMenuItemRepresentedObject(option), GetStringForKey(fontOptions[i].option)) == 0);
		}
		WMResizeWidget(window, size.width + 204, size.height + 204);
		bounds(WMWidgetView(panel->box));
		WMResizeWidget(window, size.width + 4, size.height + 4);
		bounds(WMWidgetView(panel->box));
		prepareForClose((Panel *)panel);
		WMDestroyWidget(window);
		wfree(panel);
	}
	WMReleasePropList(domain);
	WMReleaseApplication();
	XCloseDisplay(display);
	{
		char path[1024];

		snprintf(path, sizeof(path), "%s/Defaults/WMGLOBAL", root);
		assert(unlink(path) == 0);
		snprintf(path, sizeof(path), "%s/Defaults", root);
		assert(rmdir(path) == 0);
		assert(rmdir(root) == 0);
	}
	puts("Font-aware font configuration tests passed");
	return 0;
}
