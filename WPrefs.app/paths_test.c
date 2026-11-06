#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "Paths.c"

static WMPropList *domain;

WMPropList *GetObjectForKey(const char *key)
{
	WMPropList *name = WMCreatePLString(key);
	WMPropList *value = WMGetFromPLDictionary(domain, name);

	WMReleasePropList(name);
	return value;
}

void SetObjectForKey(WMPropList *value, const char *key)
{
	WMPropList *name = WMCreatePLString(key);

	WMPutInPLDictionary(domain, name, value);
	WMReleasePropList(name);
}

void AddSection(Panel *panel, const char *iconFile)
{
	(void)panel;
	(void)iconFile;
}

static void checkBounds(WMView *view)
{
	for (WMView *child = view->childrenList; child; child = child->nextSister) {
		assert(child->pos.x >= 0 && child->pos.y >= 0);
		assert(child->pos.x + child->size.width <= view->size.width);
		assert(child->pos.y + child->size.height <= view->size.height);
		if (child->self && WMWidgetClass(child->self) == WC_Button) {
			WMSize size = WMGetButtonPreferredSize(child->self);

			assert(size.width <= child->size.width && size.height <= child->size.height);
		}
		checkBounds(child);
	}
}

static void testRowDrawing(_Panel *panel)
{
	WMScreen *screen = WMWidgetScreen(panel->box);
	int height = WMGetListItemHeight(panel->icoL);
	Pixmap canvas = XCreatePixmap(screen->display, W_DRAWABLE(screen), 200, height, screen->depth);
	WMRect rect = wmkrect(0, 0, 200, height);
	RImage *image;
	int redPixels = 0;

	paintItem(panel->icoL, 0, canvas, "/missing/path", 1, &rect);
	image = RCreateImageFromDrawable(screen->rcontext, canvas, None);
	assert(image);
	for (int y = 0; y < image->height; y++)
		for (int x = 0; x < image->width; x++) {
			unsigned char *pixel = image->data + 3 * (y * image->width + x);

			if (pixel[0] > 150 && pixel[1] < 100 && pixel[2] < 100)
				redPixels++;
		}
	assert(redPixels > height);
	RReleaseImage(image);
	XFreePixmap(screen->display, canvas);
}

static void testPanel(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };

	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WMWindow *window;
		_Panel *panel;
		WMPropList *array, *path;
		WMSize size;

		domain = WMCreatePLDictionary(NULL, NULL);
		path = WMCreatePLString("/");
		array = WMCreatePLArray(path, NULL);
		SetObjectForKey(array, "IconPath");
		SetObjectForKey(array, "PixmapPath");
		WMReleasePropList(path);
		WMReleasePropList(array);
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		window = WMCreateWindow(screen, "pathsTest");
		WMRealizeWidget(window);
		panel = (_Panel *)InitPaths(window);
		assert(panel->callbacks.automaticLayout);
		createPanel((Panel *)panel);
		size = WMGetBoxMinimumSize(panel->box);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width + 4, size.height + 4);
		checkBounds(WMWidgetView(panel->box));
		assert(WMGetListItemHeight(panel->icoL) >= WMFontHeight(panel->font) + 4);
		assert(!WMGetListItem(panel->icoL, 0)->uflags);
		addPathToList(panel->icoL, 1, "/wmaker-test-directory-that-does-not-exist");
		assert(WMGetListItem(panel->icoL, 1)->uflags == 1);
		addPathToList(panel->icoL, 1, "/usr");
		WMSelectListItem(panel->icoL, 1);
		WMPerformButtonClick(panel->icorB);
		assert(WMGetListNumberOfRows(panel->icoL) == 2);
		assert(strcmp(WMGetListItem(panel->icoL, 1)->text,
			      "/wmaker-test-directory-that-does-not-exist") == 0);
		WMSelectTabViewItemAtIndex(panel->tabv, 1);
		assert(WMGetTabViewItemIdentifier(WMGetSelectedTabViewItem(panel->tabv)) == 1);
		addPathToList(panel->pixL, -1, "/usr");
		WMSelectListItem(panel->pixL, 0);
		WMPerformButtonClick(panel->pixrB);
		storeData(panel);
		assert(WMGetPropListItemCount(GetObjectForKey("IconPath")) == 2);
		assert(strcmp(WMGetFromPLString(WMGetFromPLArray(GetObjectForKey("PixmapPath"), 0)), "/usr") == 0);
		WMSelectTabViewItemAtIndex(panel->tabv, 0);
		testRowDrawing(panel);
		{
			int height = WMWidgetHeight(panel->icoL);
			int width = WMWidgetWidth(panel->icoL);

			WMResizeWidget(window, size.width + 204, size.height + 204);
			assert(WMWidgetHeight(panel->icoL) > height && WMWidgetWidth(panel->icoL) > width);
			checkBounds(WMWidgetView(panel->box));
		}
		WMDestroyWidget(window);
		WMReleaseFont(panel->font);
		WMReleaseColor(panel->red);
		WMReleaseColor(panel->black);
		WMReleaseColor(panel->white);
		WMReleaseColor(panel->gray);
		wfree(panel);
		WMReleasePropList(domain);
	}
}

int main(int argc, char **argv)
{
	Display *display;
	WMScreen *screen;

	if (!getenv("DISPLAY")) {
		puts("SKIP: paths_test needs an X server");
		return 77;
	}
	WMInitializeApplication("paths_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPanel(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Font-aware search path tests passed");
	return 0;
}
