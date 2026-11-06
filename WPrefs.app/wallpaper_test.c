#include "WPrefs.h"

#undef NDEBUG
#include <assert.h>

#include "WallpaperChooser.c"

static char *root;
static char *userFolder;
static int activations;
static int hookCalls;

static void stubActivate(WallpaperChooser *ch, WMPropList *domain)
{
	(void)ch;
	assert(WMIsPLDictionary(domain));
	activations++;
}

static void countHook(const char *key, WMPropList *value, void *data)
{
	(void)data;
	assert(key && value);
	hookCalls++;
}

static void writeImage(const char *path, int width, int height, unsigned char r, unsigned char g, unsigned char b)
{
	RColor color = { r, g, b, 0xff };
	RImage *image = RCreateImage(width, height, False);

	RClearImage(image, &color);
	assert(RSaveImage(image, path, "PNG"));
	RReleaseImage(image);
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

static void drainThumbnails(WallpaperChooser *ch)
{
	while (ch->thumbTimer) {
		WMDeleteTimerHandler(ch->thumbTimer);
		thumbnailStep(ch);
	}
}

static WallpaperMonitor *testMonitors(void)
{
	WallpaperMonitor *monitors = wmalloc(sizeof(WallpaperMonitor) * 2);

	monitors[0].rect = wmkrect(0, 0, 1000, 500);
	monitors[0].name = wstrdup("Left");
	monitors[1].rect = wmkrect(1000, 0, 500, 500);
	monitors[1].name = wstrdup("Right-1");
	return monitors;
}

static RImage *previewImage(WallpaperChooser *ch)
{
	WMPixmap *pixmap = WMGetLabelImage(ch->previewL);
	RImage *image;

	assert(pixmap);
	image = RCreateImageFromDrawable(WMScreenRContext(ch->screen), WMGetPixmapXID(pixmap), None);
	assert(image);
	assert(image->width == ch->previewWidth && image->height == ch->previewHeight);
	return image;
}

static void previewPixel(WallpaperChooser *ch, int monitor, int fx, int fy, unsigned char rgb[3])
{
	RImage *image = previewImage(ch);
	WMRect r = previewRect(ch, monitor);
	int x = r.pos.x + (int)r.size.width * fx / 100, y = r.pos.y + (int)r.size.height * fy / 100;
	unsigned char *p = image->data + (y * image->width + x) * (image->format == RRGBAFormat ? 4 : 3);

	memcpy(rgb, p, 3);
	RReleaseImage(image);
}

static Bool isRed(WallpaperChooser *ch, int monitor, int fx, int fy)
{
	unsigned char p[3];

	previewPixel(ch, monitor, fx, fy, p);
	return p[0] > 200 && p[1] < 60 && p[2] < 60;
}

static Bool isBlack(WallpaperChooser *ch, int monitor, int fx, int fy)
{
	unsigned char p[3];

	previewPixel(ch, monitor, fx, fy, p);
	return p[0] < 30 && p[1] < 30 && p[2] < 30;
}

static WMPropList *readDomainKey(const char *name)
{
	char *path = wdefaultspathfordomain("WindowMaker");
	WMPropList *domain = WMReadPropListFromFile(path);
	WMPropList *value = NULL;

	wfree(path);
	assert(domain && WMIsPLDictionary(domain));
	value = domainValue(domain, name);
	if (value)
		WMRetainPropList(value);
	WMReleasePropList(domain);
	return value;
}

static void assertTexture(WMPropList *texture, const char *type, const char *path)
{
	assert(texture && WMIsPLArray(texture) && WMGetPropListItemCount(texture) == 3);
	assert(strcmp(WMGetFromPLString(WMGetFromPLArray(texture, 0)), type) == 0);
	assert(strcmp(WMGetFromPLString(WMGetFromPLArray(texture, 1)), path) == 0);
	assert(strcmp(WMGetFromPLString(WMGetFromPLArray(texture, 2)), TEXTURE_COLOR) == 0);
}

static int rowNamed(WallpaperChooser *ch, const char *name)
{
	for (int i = 0; i < ch->itemCount; i++)
		if (strcmp(ch->items[i]->name, name) == 0)
			return i;
	assert(!"row not found");
	return -1;
}

static void testRowDrawing(WallpaperChooser *ch, int row)
{
	Display *dpy = WMScreenDisplay(ch->screen);
	int height = WMGetListItemHeight(ch->list), width = ch->thumbWidth + 200, red = 0;
	Pixmap canvas = XCreatePixmap(dpy, W_DRAWABLE(ch->screen), width, height, ch->screen->depth);
	WMRect rect = wmkrect(0, 0, width, height);
	RImage *image;

	paintItem(ch->list, row, canvas, ch->items[row]->name, 0, &rect);
	image = RCreateImageFromDrawable(WMScreenRContext(ch->screen), canvas, None);
	assert(image);
	for (int i = 0; i < width * height; i++) {
		unsigned char *p = image->data + i * (image->format == RRGBAFormat ? 4 : 3);

		if (p[0] > 200 && p[1] < 60 && p[2] < 60)
			red++;
	}
	/* The 2:1 thumbnail fills the full thumbnail width. */
	assert(red >= ch->thumbWidth * (ch->thumbWidth / 2 - 2));
	RReleaseImage(image);
	XFreePixmap(dpy, canvas);
}

static void testDefaultFolder(void)
{
	char *folder = WallpaperChooserDefaultFolder();
	char *path;

	assert(strcmp(folder, userFolder) != 0 ? folderHasImages(folder) : True);
	wfree(folder);
	assert(makeDirectories(userFolder));
	assert(makeDirectories(formatString("%s/sub", userFolder)));
	path = formatString("%s/wide.png", userFolder);
	writeImage(path, 400, 200, 0xff, 0, 0);
	wfree(path);
	path = formatString("%s/sub/square.png", userFolder);
	writeImage(path, 100, 100, 0, 0xff, 0);
	wfree(path);
	path = formatString("%s/photo.png", userFolder);
	writeImage(path, 800, 600, 0, 0, 0xff);
	wfree(path);
	path = formatString("%s/notes.txt", userFolder);
	fclose(fopen(path, "w"));
	wfree(path);
	folder = WallpaperChooserDefaultFolder();
	assert(strcmp(folder, userFolder) == 0);
	wfree(folder);
}

static void testDisplayNames(void)
{
	char *name = displayName("Altai/contents/images/", "5120x2880.png");

	assert(strcmp(name, "Altai (5120x2880)") == 0);
	wfree(name);
	name = displayName("sub/", "beach.jpeg");
	assert(strcmp(name, "sub/beach") == 0);
	wfree(name);
}

static void testMonitorParsing(void)
{
	WallpaperMonitor monitor;

	assert(readMonitorLine("1920 0 2560 1440 DP-1\n", &monitor));
	assert(monitor.rect.pos.x == 1920 && monitor.rect.size.height == 1440);
	assert(strcmp(monitor.name, "DP-1") == 0);
	wfree(monitor.name);
	assert(!readMonitorLine("garbage\n", &monitor));
	assert(!readMonitorLine("0 0 0 10 empty\n", &monitor));
}

static void testChooser(WMScreen *screen)
{
	int sizes[] = { 11, 24, 32 };
	WMSize previous = { 0, 0 };
	char *domainPath = wdefaultspathfordomain("WindowMaker");
	char *wide = formatString("%s/wide.png", userFolder);

	for (int i = 0; i < 3; i++) {
		WMFont *font = WMSystemFontOfSize(screen, sizes[i]);
		WallpaperChooser *ch, *again;
		WMWindow *window;
		WMPropList *value;
		WMSize size;
		int row;

		unlink(domainPath);
		WMSetWidgetDefaultFont(screen, font);
		WMReleaseFont(font);
		window = WMCreateWindow(screen, "wallpaperTest");
		WMRealizeWidget(window);
		ch = createChooser(window, testMonitors(), 2, 1);
		ch->activate = stubActivate;
		ch->desktopGrabbed = True;
		WallpaperChooserSetApplyHook(ch, countHook, NULL);

		size = WallpaperChooserGetMinimumSize(ch);
		assert(size.width > previous.width && size.height > previous.height);
		previous = size;
		WMResizeWidget(window, size.width, size.height);
		WMResizeWidget(ch->box, size.width, size.height);
		WMRealizeWidget(ch->box);
		checkBounds(WMWidgetView(ch->box));
		assert(ch->thumbHeight >= 3 * WMFontHeight(ch->font));
		assert(WMGetListItemHeight(ch->list) > ch->thumbHeight);
		/* Two 1000x500 + 500x500 monitors form a 3:1 desktop. */
		assert(abs(ch->previewWidth - 3 * ch->previewHeight) <= 3);
		assert(ch->thisMonitorB && !WMGetButtonSelected(ch->thisMonitorB));
		assert(strstr(WMGetButtonText(ch->thisMonitorB), "Right-1 (500x500)"));
		assert(WMGetButtonSelected(ch->modeB[MODE_SCALE]));

		WallpaperChooserSetFolder(ch, userFolder);
		assert(WMWidgetIsMapped(ch->progress) || !W_VIEW(ch->progress)->flags.realized);
		drainThumbnails(ch);
		assert(ch->itemCount == 3);
		assert(strcmp(ch->items[0]->name, "photo") == 0 && strcmp(ch->items[1]->name, "sub/square") == 0);
		assert(ch->cacheHits == (i == 0 ? 0 : 3));
		for (int j = 0; j < 3; j++) {
			WMSize thumb;

			assert(ch->items[j]->pixmap);
			thumb = WMGetPixmapSize(ch->items[j]->pixmap);
			assert((int)thumb.width <= ch->thumbWidth && (int)thumb.height <= ch->thumbHeight);
		}
		{
			int cached = 0;
			DIR *d = opendir(ch->cacheDir);
			struct dirent *entry;

			assert(d);
			while ((entry = readdir(d)))
				cached += entry->d_name[0] != '.';
			closedir(d);
			assert(cached == 3);
		}
		row = rowNamed(ch, "wide");
		testRowDrawing(ch, row);

		assert(!WMGetButtonEnabled(ch->applyB));
		WMSelectListItem(ch->list, row);
		listSelected(ch->list, ch);
		assert(ch->selectedImage && ch->selectedImage->width == 400);
		assert(WMGetButtonEnabled(ch->applyB));
		assert(isRed(ch, 0, 50, 50) && isRed(ch, 1, 50, 10));
		assert(strstr(WMGetLabelText(ch->captionL), "all 2 monitors"));

		WMPerformButtonClick(ch->thisMonitorB);
		assert(!isRed(ch, 0, 50, 50) && isRed(ch, 1, 50, 50) && isRed(ch, 1, 50, 10));
		assert(strstr(WMGetLabelText(ch->captionL), "Right-1"));
		WMPerformButtonClick(ch->modeB[MODE_FIT]);
		assert(isRed(ch, 1, 50, 50) && isBlack(ch, 1, 50, 10));
		WMPerformButtonClick(ch->modeB[MODE_FILL]);
		assert(isRed(ch, 1, 50, 10) && isRed(ch, 1, 10, 50));
		WMPerformButtonClick(ch->modeB[MODE_CENTER]);
		assert(isRed(ch, 1, 50, 50) && isBlack(ch, 1, 50, 10) && isBlack(ch, 1, 5, 50));

		if (i == 0) {
			Display *dpy = WMScreenDisplay(screen);
			Atom desktop = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
			long workspace = 1;
			WMPropList *initial = WMCreatePropListFromDescription(
				"{WorkspaceSpecificBack = ((solid, green), (solid, blue), ());}");

			assert(WMWritePropListToFile(initial, domainPath));
			WMReleasePropList(initial);
			XChangeProperty(dpy, DefaultRootWindow(dpy), desktop, XA_CARDINAL, 32, PropModeReplace,
					(unsigned char *)&workspace, 1);
			XSync(dpy, False);
			WMPerformButtonClick(ch->allMonitorsB);
			applyWallpaper(ch->applyB, ch);
			XDeleteProperty(dpy, DefaultRootWindow(dpy), desktop);
			XSync(dpy, False);
			value = readDomainKey("WorkspaceSpecificBack");
			assert(WMGetPropListItemCount(value) == 3);
			assert(strcmp(WMGetFromPLString(WMGetFromPLArray(WMGetFromPLArray(value, 0), 1)), "green") == 0);
			assertTexture(WMGetFromPLArray(value, 1), "cpixmap", wide);
			assert(WMGetPropListItemCount(WMGetFromPLArray(value, 2)) == 0);
			WMReleasePropList(value);
			assert(hookCalls == 3);
		}

		activations = hookCalls = 0;
		WMPerformButtonClick(ch->allMonitorsB);
		WMPerformButtonClick(ch->modeB[MODE_SCALE]);
		WMPerformButtonClick(ch->applyB);
		assert(activations == 1 && hookCalls == 2);
		value = readDomainKey("WorkspaceBack");
		assertTexture(value, "spixmap", wide);
		WMReleasePropList(value);
		value = readDomainKey("MonitorSpecificBack");
		assert(WMIsPLDictionary(value) && WMGetPropListItemCount(value) == 0);
		WMReleasePropList(value);

		WMPerformButtonClick(ch->thisMonitorB);
		WMPerformButtonClick(ch->modeB[MODE_TILE]);
		assert(isRed(ch, 1, 5, 5) && isRed(ch, 1, 95, 95));
		WMPerformButtonClick(ch->applyB);
		assert(activations == 2 && hookCalls == 3);
		value = readDomainKey("MonitorSpecificBack");
		assert(WMGetPropListItemCount(value) == 1);
		{
			WMPropList *key = WMCreatePLString("Right-1");

			assertTexture(WMGetFromPLDictionary(value, key), "tpixmap", wide);
			WMReleasePropList(key);
		}
		WMReleasePropList(value);
		value = readDomainKey("WorkspaceBack");
		assertTexture(value, "spixmap", wide);
		WMReleasePropList(value);

		/* A new chooser starts from the configured monitor texture. */
		again = createChooser(window, testMonitors(), 2, 1);
		again->desktopGrabbed = True;
		assert(WMGetButtonSelected(again->thisMonitorB) && WMGetButtonSelected(again->modeB[MODE_TILE]));
		WallpaperChooserSetFolder(again, userFolder);
		assert(again->selectedRow == rowNamed(again, "wide") && again->selectedImage);
		drainThumbnails(again);
		assert(again->cacheHits == 3);
		WallpaperChooserDestroy(again);

		{
			char *empty = formatString("%s/empty", root);

			assert(makeDirectories(empty));
			WMSetTextFieldText(ch->folderT, empty);
			folderEdited(ch, NULL);
			assert(ch->itemCount == 0 && ch->selectedRow < 0 && !WMGetButtonEnabled(ch->applyB));
			assert(strcmp(ch->folder, empty) == 0);
			assert(strstr(WMGetLabelText(ch->statusL), "No backgrounds"));
			wfree(empty);
		}

		WallpaperChooserDestroy(ch);
		WMDestroyWidget(window);
	}
	wfree(wide);
	wfree(domainPath);
}

static void testSingleMonitor(WMScreen *screen)
{
	WMWindow *window = WMCreateWindow(screen, "wallpaperSingle");
	WallpaperMonitor *monitor = wmalloc(sizeof(*monitor));
	WallpaperChooser *ch;

	monitor->rect = wmkrect(0, 0, 1600, 1200);
	monitor->name = NULL;
	WMRealizeWidget(window);
	ch = createChooser(window, monitor, 1, 0);
	assert(!ch->thisMonitorB && applyToAllMonitors(ch));
	assert(abs(ch->previewWidth * 3 - ch->previewHeight * 4) <= 4);
	assert(strstr(WMGetLabelText(ch->captionL), "1600x1200"));
	WallpaperChooserDestroy(ch);
	WMDestroyWidget(window);
}

int main(int argc, char **argv)
{
	char pattern[] = "/tmp/wallpaper_test.XXXXXX";
	Display *display;
	WMScreen *screen;
	char *cache;

	if (!getenv("DISPLAY")) {
		puts("SKIP: wallpaper_test needs an X server");
		return 77;
	}
	root = mkdtemp(pattern);
	assert(root);
	setenv("WMAKER_USER_ROOT", root, 1);
	cache = formatString("%s/cache", root);
	setenv("XDG_CACHE_HOME", cache, 1);
	userFolder = formatString("%s/%s/%s/Backgrounds", root, USERDATA_SUBDIR, PACKAGE_TARNAME);

	WMInitializeApplication("wallpaper_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testMonitorParsing();
	testDisplayNames();
	testDefaultFolder();
	testChooser(screen);
	testSingleMonitor(screen);
	WMReleaseApplication();
	XCloseDisplay(display);
	{
		char *command = formatString("rm -rf '%s'", root);

		assert(system(command) == 0);
		wfree(command);
	}
	puts("Font-aware wallpaper chooser tests passed");
	return 0;
}
