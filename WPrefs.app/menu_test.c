#include "WPrefs.h"
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include "Menu.c"
#include "editmenu.c"

void AddSection(Panel *panel, const char *icon)
{
	(void)panel;
	(void)icon;
}

char *capture_shortcut(Display *display, Bool *capturing, Bool convert_case)
{
	(void)display;
	assert(*capturing && !convert_case);
	return wstrdup("Control+F3");
}

static void bounds(WMView *view)
{
	if (view->self && WMWidgetClass(view->self) == WC_ScrollView)
		return;
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		if (child->pos.x + child->size.width > view->size.width ||
		    child->pos.y + child->size.height > view->size.height)
			fprintf(stderr, "Overflow: parent %u %ux%u, child %u at %d,%d size %ux%u\n",
				WMWidgetClass(view->self), view->size.width, view->size.height,
				WMWidgetClass(child->self), child->pos.x, child->pos.y, child->size.width, child->size.height);
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		bounds(child);
	}
}

static void formBounds(_Panel *panel)
{
	int bottom = 0;

	for (int type = 0; type < LastInfo; type++)
		for (int i = 0; i < MAX_SECTION_SIZE && panel->sections[type][i]; i++) {
			WMWidget *widget = panel->sections[type][i];
			Bool visible = False;

			for (int j = 0; j < MAX_SECTION_SIZE && panel->sections[panel->currentType][j]; j++)
				if (panel->sections[panel->currentType][j] == widget)
					visible = True;
			assert(W_VIEW(widget)->flags.mapped == visible);
		}
	for (int i = 0; i < MAX_SECTION_SIZE && panel->sections[panel->currentType][i]; i++) {
		WMView *view = WMWidgetView(panel->sections[panel->currentType][i]);

		assert(view->pos.y >= bottom);
		assert(view->pos.x + view->size.width <= WMWidgetWidth(panel->optionsF));
		assert(view->pos.y + view->size.height <= WMWidgetHeight(panel->optionsF));
		bounds(view);
		bottom = view->pos.y + view->size.height;
	}
	if (panel->currentType == CommandInfo) {
		WMView *list = WMWidgetView(panel->icommandL);
		WMView *extra = WMWidgetView(panel->paramF);
		WMView *shortcut = WMWidgetView(panel->shortF);

		assert(extra->pos.y >= list->pos.y + list->size.height);
		assert(shortcut->pos.y >= extra->pos.y + extra->size.height);
		bounds(extra);
	}
}

static void factoryDestroyers(WEditMenu *menu)
{
	WEditMenuItem *item;

	for (int i = 0; (item = WGetEditMenuItem(menu, i)); i++) {
		WEditMenu *submenu = WGetEditMenuSubmenu(item);

		/* Factory parameters are borrowed literals; only clones own strings. */
		WSetEditMenuItemData(item, WGetEditMenuItemData(item), wfree);
		if (submenu)
			factoryDestroyers(submenu);
	}
}

static void submenuIndicator(_Panel *panel)
{
	WEditMenu *menu = panel->factory[0];
	WMScreen *screen = WMWidgetScreen(menu);
	int height = WMAX(8, WMFontHeight(panel->normalFont) / 2);
	int width = WMAX(4, height / 2);
	XImage *image;
	int blended = 0;

	image = XGetImage(screen->display, WMGetPixmapXID(menu->cascade[0]), 0, 0,
			  width, height, AllPlanes, ZPixmap);
	assert(image);
	assert(XGetPixel(image, width / 3, height / 2) == W_PIXEL(screen->black));
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++) {
			unsigned long pixel = XGetPixel(image, x, y);

			if (pixel != W_PIXEL(screen->black) && pixel != W_PIXEL(screen->gray))
				blended++;
		}
	assert(blended > 0);
	XDestroyImage(image);
}

int main(int argc, char **argv)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	int previousCloseSize = 0;
	Display *display;
	WMScreen *screen;
	char root[] = "/tmp/wmaker-menu-test-XXXXXX";
	char *path;
	WMPropList *source;
	const char *menu =
		"(Applications, (Run, SHORTCUT, \"Control+F1\", SHEXEC, \"xterm\"),"
		"(Exit, EXIT, QUICK), (Restart, RESTART, \"other-wm\"),"
		"(External, OPEN_MENU, \"/tmp/menu\"),"
		"(Pipe, OPEN_MENU, \"|| menu-command\"),"
		"(PLPipe, OPEN_PLMENU, \"| pl-command\"),"
		"(Files, OPEN_MENU, \"-noext /tmp WITH opener\"),"
		"(Workspaces, WORKSPACE_MENU), (Windows, WINDOWS_MENU),"
		"(Nested, (Child, EXEC, \"nested-command\")))";

	if (!getenv("DISPLAY"))
		return 77;
	assert(mkdtemp(root));
	assert(setenv("WMAKER_USER_ROOT", root, 1) == 0);
	WMInitializeApplication("menu_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	source = WMCreatePropListFromDescription(menu);
	assert(source);
	path = wdefaultspathfordomain("WMRootMenu");
	assert(WMWritePropListToFile(source, path));
	for (int n = 0; n < 3; n++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[n]);
		WMWindow *window;
		WMScrollView *viewport;
		WMFrame *content;
		_Panel *panel;
		WMSize size;
		WMPropList *saved, *roundtrip;
		WEditMenu *copy;
		WEditMenuItem *item, *clone;

		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		font = WMBoldSystemFontOfSize(screen, sizes[n]);
		WMSetWidgetDefaultBoldFont(screen, font);
		WMReleaseFont(font);
		window = WMCreateWindow(screen, "menuTest");
		viewport = WMCreateScrollView(window);
		content = WMCreateFrame(window);
		WMSetScrollViewRelief(viewport, WRFlat);
		WMSetScrollViewContentView(viewport, WMWidgetView(content));
		WMSetViewExpandsToParent(WMWidgetView(viewport), 0, 0, 0, 0);
		WMRealizeWidget(window);
		panel = (_Panel *)InitMenu(content);
		assert(panel->callbacks.automaticLayout);
		createPanel(panel);
		assert(!panel->loaded && !panel->menu && !panel->menuPath);
		assert(W_VIEW(panel->box)->flags.realized);
		WMView *temporary = W_CreateUnmanagedTopView(screen);
		W_ResizeView(temporary, 100, 50);
		W_RealizeView(temporary);
		WMDrawString(screen, temporary->window, screen->black, panel->normalFont, 0, 0, "drag", 4);
		W_DestroyView(temporary);
		WMDrawString(screen, WMWidgetXID(panel->commandT), screen->black, panel->normalFont, 0, 0, "next", 4);
		XSync(display, False);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		WMResizeWidget(content, size.width + 4, size.height + 4);
		WMMapWidget(viewport);
		WMMapWidget(content);
		WMMapWidget(panel->box);
		WMMapWidget(window);
		submenuIndicator(panel);
		for (int larger = 0; larger < 2; larger++) {
			for (InfoType type = NoInfo; type < LastInfo; type++) {
				changeInfoType(panel, "Test item", type);
				formBounds(panel);
			}
			for (int palette = 0; palette < 3; palette++) {
				WMSetPopUpButtonSelectedItem(panel->typeP, palette);
				changedItemPad(panel->typeP, panel);
				assert(W_VIEW(panel->itemPad[palette])->flags.mapped);
				if (WMWidgetWidth(panel->factory[palette]) != WMGetScrollViewVisibleRect(panel->itemPad[palette]).size.width)
					fprintf(stderr, "Palette %d: factory %u, viewport %u, host %u\n", palette,
						WMWidgetWidth(panel->factory[palette]),
						WMGetScrollViewVisibleRect(panel->itemPad[palette]).size.width,
						WMWidgetWidth(panel->padHost));
				assert(WMWidgetWidth(panel->factory[palette]) ==
				       WMGetScrollViewVisibleRect(panel->itemPad[palette]).size.width);
			}
			WMResizeWidget(window, size.width + 204, size.height + 104);
			WMResizeWidget(content, size.width + 204, size.height + 104);
		}
		changeInfoType(panel, NULL, NoInfo);
		WMResizeWidget(window, 900, 500);
		WMRect visible = WMGetScrollViewVisibleRect(viewport);
		WMView *instructions = W_VIEW(panel->instructionsV);
		assert(W_VIEW(panel->box)->pos.x + W_VIEW(panel->optionsF)->pos.x +
		       instructions->pos.x + instructions->size.width <= visible.size.width);
		assert(W_VIEW(panel->box)->pos.y + W_VIEW(panel->optionsF)->pos.y +
		       instructions->pos.y + instructions->size.height <= visible.size.height);
		assert(WMWidgetWidth(panel->instructionsL) ==
		       WMGetScrollViewVisibleRect(panel->instructionsV).size.width);
		assert(WMWidgetHeight(panel->instructionsL) >=
		       WMGetLabelPreferredSizeForWidth(panel->instructionsL, WMWidgetWidth(panel->instructionsL)).height);
		WMResizeWidget(window, size.width + 204, size.height + 104);
		WMSetButtonText(panel->sgrabB, _("Cancel"));
		assert(WMWidgetWidth(panel->sgrabB) >= WMGetButtonPreferredSize(panel->sgrabB).width);
		WMSetButtonText(panel->sgrabB, _("Capture"));
		showMenus(panel);
		assert(panel->loaded && panel->menu);
		WMFont *noteFont = WMGetLabelFont(panel->saveLocationL);
		double instructionPixels, notePixels;
		assert(FcPatternGetDouble(panel->normalFont->font->pattern, FC_PIXEL_SIZE, 0,
					 &instructionPixels) == FcResultMatch);
		assert(FcPatternGetDouble(noteFont->font->pattern, FC_PIXEL_SIZE, 0, &notePixels) == FcResultMatch);
		assert(fabs(notePixels - instructionPixels * 0.6) < 0.01);
		assert(strstr(WMGetLabelText(panel->saveLocationL), path));
		assert(!strstr(WMGetLabelText(panel->instructionsL), path));
		changeInfoType(panel, NULL, NoInfo);
		WMResizeWidget(window, 900, 500);
		bounds(W_VIEW(panel->instructionsContent));
		assert(WMWidgetHeight(panel->instructionsContent) >=
		       W_VIEW(panel->saveLocationL)->pos.y + WMWidgetHeight(panel->saveLocationL));
		WMResizeWidget(window, size.width + 204, size.height + 104);
		copy = panel->menu;
		showMenus(panel);
		assert(copy == panel->menu);
		hideMenus(panel);
		for (int i = 0; (item = WGetEditMenuItem(panel->menu, i)); i++) {
			menuItemSelected(&menuDelegate, panel->menu, item);
			formBounds(panel);
		}
		WEditMenu *submenu = WGetEditMenuSubmenu(WGetEditMenuItem(panel->menu, 9));
		WMWidget *close = NULL;
		WTearOffEditMenu(panel->menu, submenu);
		for (WMView *row = W_VIEW(submenu)->childrenList; row; row = row->nextSister)
			for (WMView *child = row->childrenList; child; child = child->nextSister)
				if (WMWidgetClass(child->self) == WC_Button)
					close = child->self;
		assert(close);
		assert(WMWidgetWidth(close) > previousCloseSize);
		previousCloseSize = WMWidgetWidth(close);
		bounds(W_VIEW(submenu));
		WSetEditMenuMinSize(submenu, wmksize(WMWidgetWidth(submenu) + 100, 0));
		bounds(W_VIEW(submenu));
		item = WGetEditMenuItem(panel->menu, 0);
		menuItemSelected(&menuDelegate, panel->menu, item);
		WMSetTextFieldText(panel->commandT, "updated-command");
		updateMenuItem(panel, item, panel->commandT);
		assert(strcmp(((ItemData *)WGetEditMenuItemData(item))->param.exec.command, "updated-command") == 0);
		WMSetTextFieldText(panel->shortT, "Mod1+F2");
		updateMenuItem(panel, item, panel->shortT);
		panel->currentItem = item;
		sgrabClicked(panel->sgrabB, panel);
		assert(strcmp(((ItemData *)WGetEditMenuItemData(item))->param.exec.shortcut, "Control+F3") == 0);
		sgrabClicked(panel->sclearB, panel);
		assert(!notblank(((ItemData *)WGetEditMenuItemData(item))->param.exec.shortcut));
		item = WGetEditMenuItem(panel->menu, 1);
		menuItemSelected(&menuDelegate, panel->menu, item);
		assert(W_VIEW(panel->quickB)->flags.mapped);
		WMSetButtonSelected(panel->quickB, False);
		buttonClicked(panel->quickB, panel);
		assert(!((ItemData *)WGetEditMenuItemData(item))->param.command.parameter);
		item = WGetEditMenuItem(panel->menu, 2);
		menuItemSelected(&menuDelegate, panel->menu, item);
		assert(W_VIEW(panel->paramF)->flags.mapped);
		WMSetTextFieldText(panel->paramT, "replacement-wm");
		updateMenuItem(panel, item, panel->paramT);
		assert(strcmp(((ItemData *)WGetEditMenuItemData(item))->param.command.parameter, "replacement-wm") == 0);
		clone = WInsertMenuItemWithTitle(panel->menu, 1, "Copied XTerm");
		menuItemCloned(&menuDelegate, panel->menu, WGetEditMenuItem(panel->factory[1], 0), clone);
		assert(strcmp(((ItemData *)WGetEditMenuItemData(clone))->param.exec.command,
			      "xterm -sb -sl 2000 -bg black -fg white") == 0);
		WRemoveEditMenuItem(panel->menu, clone);
		saved = buildPLFromMenu(panel);
		copy = buildSubmenu(panel, saved);
		roundtrip = processSubmenu(copy);
		assert(WMIsPropListEqualTo(saved, roundtrip));
		WMReleasePropList(roundtrip);
		WMDestroyWidget(copy);
		storeData(panel);
		roundtrip = WMReadPropListFromFile(path);
		assert(WMIsPropListEqualTo(saved, roundtrip));
		WMReleasePropList(roundtrip);
		WMReleasePropList(saved);
		assert(WMWritePropListToFile(source, path));
		WMRemoveNotificationObserver(panel);
		for (int i = 0; i < 3; i++)
			factoryDestroyers(panel->factory[i]);
		WMDestroyWidget(panel->menu);
		WMDestroyWidget(window);
		for (int i = 0; i < LastInfo; i++)
			if (panel->markerPix[i])
				WMReleasePixmap(panel->markerPix[i]);
		WMReleaseFont(panel->normalFont);
		WMReleaseFont(panel->boldFont);
		wfree(panel->menuPath);
		wfree(panel);
	}
	WMReleasePropList(source);
	assert(unlink(path) == 0);
	wfree(path);
	char defaults[sizeof(root) + 16];
	snprintf(defaults, sizeof(defaults), "%s/Defaults", root);
	assert(rmdir(defaults) == 0);
	assert(rmdir(root) == 0);
	puts("PASS: menu layouts, wrapped instructions, smooth indicators, palettes, edits and saved roundtrip at 11/24/32.");
	return 0;
}
