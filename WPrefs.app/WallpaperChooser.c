/* WallpaperChooser.c - browse a folder of images and set the wallpaper
 *
 *  WPrefs - Window Maker Preferences Program
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
#include "WallpaperChooser.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/Xatom.h>

#define THUMB_CACHE_SIZE	256
#define MAX_SCAN_DEPTH		3
#define MAX_SCAN_FILES		2000
#define THUMB_STEP_MS		40
#define TEXTURE_COLOR		"black"

typedef struct {
	WMRect rect;
	char *name;
} WallpaperMonitor;

typedef struct {
	char *path;
	char *name;
	WMPixmap *pixmap;
	Bool loaded;
} WallpaperItem;

enum {
	MODE_SCALE,
	MODE_TILE,
	MODE_CENTER,
	MODE_FIT,
	MODE_FILL,
	MODE_COUNT
};

static const struct {
	const char *type;
	const char *label;
} wallpaperModes[MODE_COUNT] = {
	{ "spixmap", N_("Scaled") },
	{ "tpixmap", N_("Tiled") },
	{ "cpixmap", N_("Centered") },
	{ "mpixmap", N_("Fit") },
	{ "fpixmap", N_("Fill") }
};

struct WallpaperChooser {
	WMScreen *screen;
	WMWidget *parent;
	WMBox *box;
	WMFont *font;
	WMColor *black;
	WMColor *white;
	WMColor *gray;
	WMColor *darkGray;

	WMTextField *folderT;
	WMButton *browseB;
	WMButton *modeB[MODE_COUNT];
	WMButton *thisMonitorB;
	WMButton *allMonitorsB;
	WMList *list;
	WMLabel *previewL;
	WMLabel *captionL;
	WMProgressIndicator *progress;
	WMLabel *statusL;
	WMButton *applyB;

	int thumbWidth;
	int thumbHeight;
	int previewWidth;
	int previewHeight;

	WallpaperMonitor *monitors;
	int monitorCount;
	int currentMonitor;
	WMRect desktop;
	RImage *desktopImage;
	Bool desktopGrabbed;

	WallpaperItem **items;
	int itemCount;
	int nextThumb;
	int cacheHits;
	WMHandlerID thumbTimer;

	char *folder;
	char *cacheDir;
	char *selectPath;
	RImage *selectedImage;
	int selectedRow;

	WallpaperChooserApplyHook *hook;
	void *hookData;
	/* Makes the new defaults visible on screen; replaced by tests. */
	void (*activate)(WallpaperChooser *chooser, WMPropList *domain);
};

static void updatePreview(WallpaperChooser *ch);

static char *formatString(const char *format, ...)
{
	va_list args;
	char *buffer;
	int length;

	va_start(args, format);
	length = vsnprintf(NULL, 0, format, args);
	va_end(args);
	buffer = wmalloc(length + 1);
	va_start(args, format);
	vsnprintf(buffer, length + 1, format, args);
	va_end(args);
	return buffer;
}

static Bool isSupportedImage(const char *name)
{
	static const struct {
		const char *format;
		const char *ext[4];
	} table[] = {
		{ "PNG", { "png" } },
		{ "JPEG", { "jpg", "jpeg", "jpe" } },
		{ "TIFF", { "tif", "tiff" } },
		{ "XPM", { "xpm" } },
		{ "GIF", { "gif" } },
		{ "WEBP", { "webp" } },
		{ "JXL", { "jxl" } },
		{ "PNM", { "pnm", "ppm", "pgm", "pbm" } },
		{ "PPM", { "ppm" } }
	};
	const char *ext = strrchr(name, '.');
	char **formats;
	int i, j, k;

	if (!ext || ext == name)
		return False;
	ext++;
	formats = RSupportedFileFormats();
	for (i = 0; i < wlengthof(table); i++) {
		for (j = 0; j < 4 && table[i].ext[j]; j++) {
			if (strcasecmp(ext, table[i].ext[j]) != 0)
				continue;
			for (k = 0; formats[k]; k++)
				if (strcmp(formats[k], table[i].format) == 0)
					return True;
		}
	}
	return False;
}

static int compareItems(const void *a, const void *b)
{
	const WallpaperItem *ia = *(WallpaperItem * const *)a;
	const WallpaperItem *ib = *(WallpaperItem * const *)b;

	return strcoll(ia->name, ib->name);
}

/* Wallpaper packages (Name/contents/images/WxH.png) are shown as "Name (WxH)". */
static char *displayName(const char *prefix, const char *file)
{
	static const char package[] = "/contents/images/";
	size_t length = strlen(prefix);
	char *base = wstrdup(file), *dot = strrchr(base, '.'), *name;

	if (dot && dot != base)
		*dot = 0;
	if (length > sizeof(package) - 1 && strcmp(prefix + length - (sizeof(package) - 1), package) == 0)
		name = formatString("%.*s (%s)", (int)(length - (sizeof(package) - 1)), prefix, base);
	else
		name = formatString("%s%s", prefix, base);
	wfree(base);
	return name;
}

/* Collects images below dir; items may be NULL to only count them. */
static void scanFolder(const char *dir, const char *prefix, int depth, WMArray *items, int *count, int limit)
{
	DIR *d = opendir(dir);
	struct dirent *entry;

	if (!d)
		return;
	while (*count < limit && (entry = readdir(d))) {
		struct stat st;
		char *path;

		if (entry->d_name[0] == '.')
			continue;
		path = formatString("%s/%s", dir, entry->d_name);
		if (stat(path, &st) == 0) {
			if (S_ISDIR(st.st_mode) && depth < MAX_SCAN_DEPTH) {
				char *sub = formatString("%s%s/", prefix, entry->d_name);

				scanFolder(path, sub, depth + 1, items, count, limit);
				wfree(sub);
			} else if (S_ISREG(st.st_mode) && isSupportedImage(entry->d_name)) {
				(*count)++;
				if (items) {
					WallpaperItem *item = wmalloc(sizeof(*item));

					item->path = path;
					path = NULL;
					item->name = displayName(prefix, entry->d_name);
					WMAddToArray(items, item);
				}
			}
		}
		if (path)
			wfree(path);
	}
	closedir(d);
}

static Bool folderHasImages(const char *dir)
{
	int count = 0;

	scanFolder(dir, "", 0, NULL, &count, 1);
	return count > 0;
}

char *WallpaperChooserDefaultFolder(void)
{
	const char *system[] = {
		WMAKER_RESOURCE_PATH "/Backgrounds",
		"/usr/share/" PACKAGE_TARNAME "/Backgrounds",
		"/usr/local/share/" PACKAGE_TARNAME "/Backgrounds",
		"/usr/share/backgrounds",
		"/usr/share/wallpapers",
		"/usr/local/share/backgrounds",
		"/usr/local/share/wallpapers"
	};
	char *user = formatString("%s/%s/%s/Backgrounds", wusergnusteppath(), USERDATA_SUBDIR, PACKAGE_TARNAME);
	int i;

	if (folderHasImages(user))
		return user;
	for (i = 0; i < wlengthof(system); i++) {
		if (folderHasImages(system[i])) {
			wfree(user);
			return wstrdup(system[i]);
		}
	}
	return user;
}

static Bool makeDirectories(const char *path)
{
	char *copy = wstrdup(path);
	char *p;
	Bool ok = True;

	for (p = copy + 1; ok && *p; p++) {
		if (*p != '/')
			continue;
		*p = 0;
		if (mkdir(copy, 0700) != 0 && errno != EEXIST)
			ok = False;
		*p = '/';
	}
	if (ok && mkdir(copy, 0700) != 0 && errno != EEXIST)
		ok = False;
	wfree(copy);
	return ok;
}

static char *thumbnailCacheDir(void)
{
	const char *base = getenv("XDG_CACHE_HOME");

	if (base && base[0] == '/')
		return formatString("%s/%s/wallpapers", base, PACKAGE_TARNAME);
	return formatString("%s/.cache/%s/wallpapers", wgethomedir(), PACKAGE_TARNAME);
}

static const char *cacheFormat(void)
{
	char **formats = RSupportedFileFormats();
	int i;

	for (i = 0; formats[i]; i++)
		if (strcmp(formats[i], "PNG") == 0)
			return "PNG";
	return "XPM";
}

/* The key changes whenever the image is modified, so stale thumbnails are never used. */
static char *thumbnailCachePath(WallpaperChooser *ch, const char *path)
{
	const char *format = cacheFormat();
	uint64_t hash = 1469598103934665603ULL;
	struct stat st;
	char *key;
	const char *c;

	if (stat(path, &st) != 0)
		return NULL;
	key = formatString("%s\n%lld\n%lld\n%d", path, (long long)st.st_mtime, (long long)st.st_size,
			   THUMB_CACHE_SIZE);
	for (c = key; *c; c++) {
		hash ^= (unsigned char)*c;
		hash *= 1099511628211ULL;
	}
	wfree(key);
	return formatString("%s/%016llx.%s", ch->cacheDir, (unsigned long long)hash,
			    strcmp(format, "PNG") == 0 ? "png" : "xpm");
}

static void flattenAlpha(RImage *image)
{
	RColor black = { 0, 0, 0, 0xff };

	if (image && image->format == RRGBAFormat)
		RCombineImageWithColor(image, &black);
}

static RImage *scaleImage(RImage *image, int width, int height)
{
	width = WMAX(1, width);
	height = WMAX(1, height);
	if (width == image->width && height == image->height)
		return RRetainImage(image);
	return RSmoothScaleImage(image, width, height);
}

static RImage *fitImage(RImage *image, int width, int height)
{
	if (image->width <= width && image->height <= height)
		return RRetainImage(image);
	if (image->width * height > image->height * width)
		return scaleImage(image, width, (int)((long long)width * image->height / image->width));
	return scaleImage(image, (int)((long long)height * image->width / image->height), height);
}

static RImage *loadThumbnail(WallpaperChooser *ch, WallpaperItem *item)
{
	RContext *rc = WMScreenRContext(ch->screen);
	char *cachePath = thumbnailCachePath(ch, item->path);
	RImage *image, *thumb;

	if (cachePath && access(cachePath, R_OK) == 0) {
		thumb = RLoadImage(rc, cachePath, 0);
		if (thumb) {
			ch->cacheHits++;
			wfree(cachePath);
			return thumb;
		}
	}
	image = RLoadImage(rc, item->path, 0);
	if (!image) {
		if (cachePath)
			wfree(cachePath);
		return NULL;
	}
	flattenAlpha(image);
	thumb = fitImage(image, THUMB_CACHE_SIZE, THUMB_CACHE_SIZE);
	RReleaseImage(image);
	if (thumb && cachePath && makeDirectories(ch->cacheDir)) {
		char *temp = formatString("%s.%d.tmp", cachePath, (int)getpid());

		if (RSaveImage(thumb, temp, cacheFormat()))
			rename(temp, cachePath);
		else
			unlink(temp);
		wfree(temp);
	}
	if (cachePath)
		wfree(cachePath);
	return thumb;
}

static void setStatus(WallpaperChooser *ch, const char *text)
{
	WMSetLabelText(ch->statusL, text);
}

static void finishThumbnails(WallpaperChooser *ch)
{
	char *text;

	WMUnmapWidget(ch->progress);
	if (ch->itemCount == 0)
		text = formatString(_("No backgrounds found in this folder."));
	else
		text = formatString(_("Backgrounds found: %d"), ch->itemCount);
	setStatus(ch, text);
	wfree(text);
}

static void thumbnailStep(void *data)
{
	WallpaperChooser *ch = data;
	struct timeval start, now;
	char *text;

	ch->thumbTimer = NULL;
	gettimeofday(&start, NULL);
	while (ch->nextThumb < ch->itemCount) {
		WallpaperItem *item = ch->items[ch->nextThumb++];
		RImage *thumb = loadThumbnail(ch, item);

		item->loaded = True;
		if (thumb) {
			RImage *small = fitImage(thumb, ch->thumbWidth, ch->thumbHeight);

			if (small) {
				item->pixmap = WMCreatePixmapFromRImage(ch->screen, small, 0);
				RReleaseImage(small);
			}
			RReleaseImage(thumb);
		}
		gettimeofday(&now, NULL);
		if ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000 > THUMB_STEP_MS)
			break;
	}
	WMRedisplayWidget(ch->list);
	if (ch->nextThumb >= ch->itemCount) {
		finishThumbnails(ch);
		return;
	}
	WMSetProgressIndicatorValue(ch->progress, ch->nextThumb);
	text = formatString(_("Generating previews... %d/%d"), ch->nextThumb, ch->itemCount);
	setStatus(ch, text);
	wfree(text);
	ch->thumbTimer = WMAddTimerHandler(1, thumbnailStep, ch);
}

static void freeItems(WallpaperChooser *ch)
{
	int i;

	if (ch->thumbTimer) {
		WMDeleteTimerHandler(ch->thumbTimer);
		ch->thumbTimer = NULL;
	}
	WMClearList(ch->list);
	for (i = 0; i < ch->itemCount; i++) {
		WallpaperItem *item = ch->items[i];

		if (item->pixmap)
			WMReleasePixmap(item->pixmap);
		wfree(item->path);
		wfree(item->name);
		wfree(item);
	}
	if (ch->items)
		wfree(ch->items);
	ch->items = NULL;
	ch->itemCount = 0;
	ch->nextThumb = 0;
}

static void clearSelection(WallpaperChooser *ch)
{
	if (ch->selectedImage)
		RReleaseImage(ch->selectedImage);
	ch->selectedImage = NULL;
	ch->selectedRow = -1;
	WMSetButtonEnabled(ch->applyB, False);
}

static void selectRow(WallpaperChooser *ch, int row)
{
	WallpaperItem *item;
	RImage *image;

	clearSelection(ch);
	if (row < 0 || row >= ch->itemCount) {
		updatePreview(ch);
		return;
	}
	item = WMGetListItem(ch->list, row)->clientData;
	image = RLoadImage(WMScreenRContext(ch->screen), item->path, 0);
	if (!image) {
		char *text = formatString(_("Could not load %s: %s"), item->name, RMessageForError(RErrorCode));

		setStatus(ch, text);
		wfree(text);
	} else {
		flattenAlpha(image);
		ch->selectedImage = image;
		ch->selectedRow = row;
		WMSetButtonEnabled(ch->applyB, True);
	}
	updatePreview(ch);
}

void WallpaperChooserSetFolder(WallpaperChooser *ch, const char *folder)
{
	WMArray *found = WMCreateArray(64);
	char *expanded = wexpandpath(folder);
	int count = 0, i, selected = -1;

	freeItems(ch);
	clearSelection(ch);
	if (ch->folder)
		wfree(ch->folder);
	ch->folder = wstrdup(folder);
	WMSetTextFieldText(ch->folderT, folder);

	scanFolder(expanded, "", 0, found, &count, MAX_SCAN_FILES);
	wfree(expanded);
	ch->itemCount = WMGetArrayItemCount(found);
	ch->items = wmalloc(sizeof(WallpaperItem *) * WMAX(1, ch->itemCount));
	for (i = 0; i < ch->itemCount; i++)
		ch->items[i] = WMGetFromArray(found, i);
	WMFreeArray(found);
	qsort(ch->items, ch->itemCount, sizeof(WallpaperItem *), compareItems);
	for (i = 0; i < ch->itemCount; i++) {
		WMListItem *row = WMAddListItem(ch->list, ch->items[i]->name);

		row->clientData = ch->items[i];
		if (ch->selectPath && strcmp(ch->selectPath, ch->items[i]->path) == 0)
			selected = i;
	}

	if (selected >= 0) {
		WMSelectListItem(ch->list, selected);
		WMSetListPosition(ch->list, selected);
		selectRow(ch, selected);
	} else {
		updatePreview(ch);
	}
	if (ch->itemCount > 0) {
		WMSetProgressIndicatorMaxValue(ch->progress, ch->itemCount);
		WMSetProgressIndicatorValue(ch->progress, 0);
		WMMapWidget(ch->progress);
		setStatus(ch, _("Generating previews..."));
		ch->thumbTimer = WMAddTimerHandler(1, thumbnailStep, ch);
	} else {
		finishThumbnails(ch);
	}
}

void WallpaperChooserStart(WallpaperChooser *ch)
{
	char *folder;

	if (ch->folder)
		return;
	folder = WallpaperChooserDefaultFolder();
	WallpaperChooserSetFolder(ch, folder);
	wfree(folder);
}

static int selectedMode(WallpaperChooser *ch)
{
	int i;

	for (i = 0; i < MODE_COUNT; i++)
		if (WMGetButtonSelected(ch->modeB[i]))
			return i;
	return MODE_SCALE;
}

static Bool applyToAllMonitors(WallpaperChooser *ch)
{
	return !ch->thisMonitorB || !WMGetButtonSelected(ch->thisMonitorB);
}

static WMRect previewRect(WallpaperChooser *ch, int i)
{
	WMRect m = ch->monitors[i].rect, d = ch->desktop;
	long long x0 = (long long)(m.pos.x - d.pos.x) * ch->previewWidth / d.size.width;
	long long y0 = (long long)(m.pos.y - d.pos.y) * ch->previewHeight / d.size.height;
	long long x1 = (long long)(m.pos.x + (int)m.size.width - d.pos.x) * ch->previewWidth / d.size.width;
	long long y1 = (long long)(m.pos.y + (int)m.size.height - d.pos.y) * ch->previewHeight / d.size.height;

	return wmkrect((int)x0, (int)y0, WMAX(1, (int)(x1 - x0)), WMAX(1, (int)(y1 - y0)));
}

static void fillRect(RImage *image, WMRect r, const RColor *color)
{
	int x, y, bpp = image->format == RRGBAFormat ? 4 : 3;

	for (y = WMAX(0, r.pos.y); y < WMIN((int)image->height, r.pos.y + (int)r.size.height); y++) {
		unsigned char *p = image->data + (y * image->width + WMAX(0, r.pos.x)) * bpp;

		for (x = WMAX(0, r.pos.x); x < WMIN((int)image->width, r.pos.x + (int)r.size.width); x++) {
			p[0] = color->red;
			p[1] = color->green;
			p[2] = color->blue;
			if (bpp == 4)
				p[3] = 0xff;
			p += bpp;
		}
	}
}

static void placeImage(RImage *canvas, RImage *image, int x, int y, WMRect clip)
{
	int sx = 0, sy = 0, w = image->width, h = image->height;
	int right = clip.pos.x + (int)clip.size.width, bottom = clip.pos.y + (int)clip.size.height;

	if (x < clip.pos.x) {
		sx = clip.pos.x - x;
		w -= sx;
		x = clip.pos.x;
	}
	if (y < clip.pos.y) {
		sy = clip.pos.y - y;
		h -= sy;
		y = clip.pos.y;
	}
	w = WMIN(w, right - x);
	h = WMIN(h, bottom - y);
	if (w > 0 && h > 0)
		RCopyArea(canvas, image, sx, sy, w, h, x, y);
}

/* Mirrors wmsetbg's per-monitor placement, at preview scale. */
static void renderWallpaper(WallpaperChooser *ch, RImage *canvas, int monitor, Bool all)
{
	RColor black = { 0, 0, 0, 0xff };
	RImage *image = ch->selectedImage, *scaled;
	WMRect r = previewRect(ch, monitor);
	int mw = ch->monitors[monitor].rect.size.width, mh = ch->monitors[monitor].rect.size.height;
	int w, h, mode = selectedMode(ch);

	fillRect(canvas, r, &black);
	if (!image)
		return;
	switch (mode) {
	case MODE_SCALE:
		w = mw;
		h = mh;
		break;
	case MODE_FIT:
	case MODE_FILL:
		if ((image->width * (long long)mh > image->height * (long long)mw) == (mode == MODE_FIT)) {
			w = mw;
			h = (int)((long long)mw * image->height / image->width);
		} else {
			w = (int)((long long)mh * image->width / image->height);
			h = mh;
		}
		break;
	default:
		w = image->width;
		h = image->height;
		break;
	}
	w = (int)((long long)w * r.size.width / mw);
	h = (int)((long long)h * r.size.height / mh);
	scaled = scaleImage(image, w, h);
	if (!scaled)
		return;
	if (mode == MODE_TILE) {
		/* wmsetbg tiles a shared texture from the root origin, but a monitor override from its corner. */
		int ox = all ? (int)((long long)-ch->desktop.pos.x * ch->previewWidth / ch->desktop.size.width) : r.pos.x;
		int oy = all ? (int)((long long)-ch->desktop.pos.y * ch->previewHeight / ch->desktop.size.height)
			     : r.pos.y;
		int x, y, sx = ox + ((r.pos.x - ox) / (int)scaled->width) * (int)scaled->width;
		int sy = oy + ((r.pos.y - oy) / (int)scaled->height) * (int)scaled->height;

		for (y = sy; y < r.pos.y + (int)r.size.height; y += scaled->height)
			for (x = sx; x < r.pos.x + (int)r.size.width; x += scaled->width)
				placeImage(canvas, scaled, x, y, r);
	} else {
		placeImage(canvas, scaled, r.pos.x + ((int)r.size.width - (int)scaled->width) / 2,
			   r.pos.y + ((int)r.size.height - (int)scaled->height) / 2, r);
	}
	RReleaseImage(scaled);
}

static int ignoreXError(Display *dpy, XErrorEvent *event)
{
	(void)dpy;
	(void)event;
	return 0;
}

/* The current root background, cropped to the monitors and scaled to the preview. */
static void grabDesktop(WallpaperChooser *ch)
{
	Display *dpy = WMScreenDisplay(ch->screen);
	Window root = DefaultRootWindow(dpy);
	Atom prop = XInternAtom(dpy, "_XROOTPMAP_ID", True), type;
	int (*handler)(Display *, XErrorEvent *);
	unsigned char *data = NULL;
	unsigned long count, after;
	int format;
	Pixmap pixmap;
	Window dummy;
	int x, y;
	unsigned int width = 0, height = 0, border, depth;
	RImage *image = NULL;

	ch->desktopGrabbed = True;
	if (prop == None || XGetWindowProperty(dpy, root, prop, 0, 1, False, XA_PIXMAP, &type, &format, &count,
					       &after, &data) != Success)
		return;
	if (!data || count != 1 || format != 32) {
		if (data)
			XFree(data);
		return;
	}
	pixmap = *(Pixmap *)data;
	XFree(data);

	XSync(dpy, False);
	handler = XSetErrorHandler(ignoreXError);
	if (XGetGeometry(dpy, pixmap, &dummy, &x, &y, &width, &height, &border, &depth) &&
	    width >= (unsigned)(ch->desktop.pos.x + (int)ch->desktop.size.width) &&
	    height >= (unsigned)(ch->desktop.pos.y + (int)ch->desktop.size.height))
		image = RCreateImageFromDrawable(WMScreenRContext(ch->screen), pixmap, None);
	XSync(dpy, False);
	XSetErrorHandler(handler);
	if (!image)
		return;
	if (ch->desktop.pos.x || ch->desktop.pos.y || image->width != ch->desktop.size.width ||
	    image->height != ch->desktop.size.height) {
		RImage *crop = RGetSubImage(image, ch->desktop.pos.x, ch->desktop.pos.y, ch->desktop.size.width,
					    ch->desktop.size.height);

		RReleaseImage(image);
		image = crop;
	}
	if (image) {
		ch->desktopImage = RScaleImage(image, ch->previewWidth, ch->previewHeight);
		RReleaseImage(image);
	}
}

static void outlineRect(RImage *image, WMRect r, const RColor *color)
{
	int x1 = r.pos.x + (int)r.size.width - 1, y1 = r.pos.y + (int)r.size.height - 1;

	RDrawLine(image, r.pos.x, r.pos.y, x1, r.pos.y, color);
	RDrawLine(image, r.pos.x, y1, x1, y1, color);
	RDrawLine(image, r.pos.x, r.pos.y, r.pos.x, y1, color);
	RDrawLine(image, x1, r.pos.y, x1, y1, color);
}

static char *monitorDescription(WallpaperChooser *ch, int i)
{
	WallpaperMonitor *m = &ch->monitors[i];

	return formatString("%s (%dx%d)", m->name ? m->name : _("screen"), (int)m->rect.size.width,
			    (int)m->rect.size.height);
}

static void updateCaption(WallpaperChooser *ch)
{
	char *text, *monitor;

	if (ch->monitorCount > 1 && applyToAllMonitors(ch)) {
		text = formatString(_("Preview of all %d monitors (%dx%d desktop)"), ch->monitorCount,
				    (int)ch->desktop.size.width, (int)ch->desktop.size.height);
	} else {
		monitor = monitorDescription(ch, ch->currentMonitor);
		text = formatString(_("Preview of monitor %s"), monitor);
		wfree(monitor);
	}
	WMSetLabelText(ch->captionL, text);
	wfree(text);
}

static void updatePreview(WallpaperChooser *ch)
{
	RColor outside = { 0x40, 0x40, 0x40, 0xff }, other = { 0x60, 0x60, 0x68, 0xff };
	RColor frame = { 0x20, 0x20, 0x20, 0xff }, highlight = { 0xff, 0xff, 0xff, 0xff };
	Bool all = applyToAllMonitors(ch);
	RImage *canvas;
	WMPixmap *pixmap;
	int i;

	if (!all && !ch->desktopGrabbed)
		grabDesktop(ch);
	canvas = RCreateImage(ch->previewWidth, ch->previewHeight, False);
	if (!canvas)
		return;
	fillRect(canvas, wmkrect(0, 0, ch->previewWidth, ch->previewHeight), &outside);
	for (i = 0; i < ch->monitorCount; i++) {
		WMRect r = previewRect(ch, i);

		if (all || i == ch->currentMonitor)
			renderWallpaper(ch, canvas, i, all);
		else if (ch->desktopImage)
			RCopyArea(canvas, ch->desktopImage, r.pos.x, r.pos.y, r.size.width, r.size.height, r.pos.x,
				  r.pos.y);
		else
			fillRect(canvas, r, &other);
	}
	for (i = 0; i < ch->monitorCount; i++)
		outlineRect(canvas, previewRect(ch, i), &frame);
	if (ch->monitorCount > 1 && !all)
		outlineRect(canvas, previewRect(ch, ch->currentMonitor), &highlight);

	pixmap = WMCreatePixmapFromRImage(ch->screen, canvas, 0);
	RReleaseImage(canvas);
	if (!pixmap)
		return;
	if (ch->monitorCount > 1) {
		WMFont *font = WMSystemFontOfSize(ch->screen, WMAX(8, WMFontHeight(ch->font) * 2 / 3));

		for (i = 0; font && i < ch->monitorCount; i++) {
			WMRect r = previewRect(ch, i);
			const char *name = ch->monitors[i].name;

			WMDrawString(ch->screen, WMGetPixmapXID(pixmap), ch->black, font, r.pos.x + 4, r.pos.y + 4,
				     name, strlen(name));
			WMDrawString(ch->screen, WMGetPixmapXID(pixmap), ch->white, font, r.pos.x + 3, r.pos.y + 3,
				     name, strlen(name));
		}
		if (font)
			WMReleaseFont(font);
	}
	WMSetLabelImage(ch->previewL, pixmap);
	WMReleasePixmap(pixmap);
	updateCaption(ch);
}

static void drawElided(WallpaperChooser *ch, Drawable d, const char *text, int x, int y, int right)
{
	static const char ellipsis[] = "...";
	int length = strlen(text), available = right - x;
	char *buffer;

	if (WMWidthOfString(ch->font, text, length) <= available) {
		WMDrawString(ch->screen, d, ch->black, ch->font, x, y, text, length);
		return;
	}
	available -= WMWidthOfString(ch->font, ellipsis, sizeof(ellipsis) - 1);
	while (length > 0) {
		/* Keep whole UTF-8 sequences. */
		do
			length--;
		while (length > 0 && ((unsigned char)text[length] & 0xc0) == 0x80);
		if (WMWidthOfString(ch->font, text, length) <= available)
			break;
	}
	buffer = formatString("%.*s%s", length, text, ellipsis);
	WMDrawString(ch->screen, d, ch->black, ch->font, x, y, buffer, strlen(buffer));
	wfree(buffer);
}

static void paintItem(WMList *list, int index, Drawable d, char *text, int state, WMRect *rect)
{
	WallpaperChooser *ch = WMGetHangedData(list);
	WallpaperItem *item = WMGetListItem(list, index)->clientData;
	Display *dpy = WMScreenDisplay(ch->screen);
	int x = rect->pos.x, y = rect->pos.y, pad = WMAX(4, WMFontHeight(ch->font) / 4);
	int tx = x + pad, ty = y + ((int)rect->size.height - ch->thumbHeight) / 2;

	XFillRectangle(dpy, d, WMColorGC((state & WLDSSelected) ? ch->white : ch->gray), x, y, rect->size.width,
		       rect->size.height);
	if (item && item->pixmap) {
		WMSize size = WMGetPixmapSize(item->pixmap);

		WMDrawPixmap(item->pixmap, d, tx + (ch->thumbWidth - (int)size.width) / 2,
			     ty + (ch->thumbHeight - (int)size.height) / 2);
	} else {
		XDrawRectangle(dpy, d, WMColorGC(ch->darkGray), tx, ty, ch->thumbWidth - 1, ch->thumbHeight - 1);
	}
	drawElided(ch, d, text, tx + ch->thumbWidth + pad * 2, y + ((int)rect->size.height - WMFontHeight(ch->font)) / 2,
		   x + (int)rect->size.width - pad);
}

static void listSelected(WMWidget *w, void *data)
{
	WallpaperChooser *ch = data;

	(void)w;
	if (WMGetListSelectedItemRow(ch->list) != ch->selectedRow)
		selectRow(ch, WMGetListSelectedItemRow(ch->list));
}

static void optionChanged(WMWidget *w, void *data)
{
	(void)w;
	updatePreview(data);
}

static void folderEdited(void *observer, WMNotification *notification)
{
	WallpaperChooser *ch = observer;
	char *text = WMGetTextFieldText(ch->folderT);

	(void)notification;
	if (text && text[0] && (!ch->folder || strcmp(text, ch->folder) != 0))
		WallpaperChooserSetFolder(ch, text);
	if (text)
		wfree(text);
}

static void browseFolder(WMWidget *w, void *data)
{
	WallpaperChooser *ch = data;
	WMOpenPanel *panel = WMGetOpenPanel(WMWidgetScreen(w));
	char *start = ch->folder ? wexpandpath(ch->folder) : wstrdup(wgethomedir());

	WMSetFilePanelCanChooseDirectories(panel, True);
	WMSetFilePanelCanChooseFiles(panel, False);
	if (WMRunModalFilePanelForDirectory(panel, ch->parent, start, _("Select Backgrounds Folder"), NULL)) {
		char *path = WMGetFilePanelFileName(panel);

		if (path) {
			WallpaperChooserSetFolder(ch, path);
			wfree(path);
		}
	}
	wfree(start);
}

static Bool windowMakerRunning(Display *dpy)
{
	Atom prop = XInternAtom(dpy, "_WINDOWMAKER_NOTICEBOARD", True), type;
	unsigned char *data = NULL;
	unsigned long count, after;
	int format;
	Bool running = False;

	if (prop != None && XGetWindowProperty(dpy, DefaultRootWindow(dpy), prop, 0, 1, False, XA_WINDOW, &type,
					       &format, &count, &after, &data) == Success && data) {
		running = count == 1;
		XFree(data);
	}
	return running;
}

static void runDetached(char *const argv[])
{
	pid_t pid = fork();

	if (pid == 0) {
		if (fork() == 0) {
			execvp(argv[0], argv);
			_exit(127);
		}
		_exit(0);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

static void activateBackground(WallpaperChooser *ch, WMPropList *domain)
{
	Display *dpy = WMScreenDisplay(ch->screen);
	WMPropList *key, *back;
	XEvent ev;

	if (windowMakerRunning(dpy)) {
		memset(&ev, 0, sizeof(ev));
		ev.xclient.type = ClientMessage;
		ev.xclient.message_type = XInternAtom(dpy, "_WINDOWMAKER_COMMAND", False);
		ev.xclient.window = DefaultRootWindow(dpy);
		ev.xclient.format = 8;
		strncpy(ev.xclient.data.b, "Reconfigure", sizeof(ev.xclient.data.b));
		XSendEvent(dpy, DefaultRootWindow(dpy), False, SubstructureRedirectMask, &ev);
		XFlush(dpy);
		return;
	}
	/* Without Window Maker, wmsetbg composes the monitor overrides from the saved domain. */
	key = WMCreatePLString("WorkspaceBack");
	back = WMGetFromPLDictionary(domain, key);
	WMReleasePropList(key);
	if (back) {
		char *texture = WMGetPropListDescription(back, False);
		char *argv[] = { "wmsetbg", "-p", texture, NULL };

		runDetached(argv);
		wfree(texture);
	}
}

static WMPropList *makeTexture(WallpaperChooser *ch, const char *path)
{
	WMPropList *type = WMCreatePLString(wallpaperModes[selectedMode(ch)].type);
	WMPropList *file = WMCreatePLString(path);
	WMPropList *color = WMCreatePLString(TEXTURE_COLOR);
	WMPropList *texture = WMCreatePLArray(type, file, color, NULL);

	WMReleasePropList(type);
	WMReleasePropList(file);
	WMReleasePropList(color);
	return texture;
}

static void storeDefault(WallpaperChooser *ch, WMPropList *domain, const char *name, WMPropList *value)
{
	WMPropList *key = WMCreatePLString(name);

	WMPutInPLDictionary(domain, key, value);
	WMReleasePropList(key);
	if (ch->hook)
		ch->hook(name, value, ch->hookData);
}

static WMPropList *domainValue(WMPropList *domain, const char *name)
{
	WMPropList *key = WMCreatePLString(name);
	WMPropList *value = WMGetFromPLDictionary(domain, key);

	WMReleasePropList(key);
	return value;
}

static int currentWorkspace(Display *dpy)
{
	Atom prop = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", True), type;
	unsigned char *data = NULL;
	unsigned long count, after;
	int format, workspace = -1;

	if (prop != None && XGetWindowProperty(dpy, DefaultRootWindow(dpy), prop, 0, 1, False, XA_CARDINAL, &type,
					       &format, &count, &after, &data) == Success && data) {
		if (count == 1 && format == 32)
			workspace = (int)*(long *)data;
		XFree(data);
	}
	return workspace;
}

static void applyWallpaper(WMWidget *w, void *data)
{
	WallpaperChooser *ch = data;
	char *path = wdefaultspathfordomain("WindowMaker");
	WMPropList *domain, *texture, *value;
	WallpaperItem *item;
	char *text, *dir;

	(void)w;
	if (ch->selectedRow < 0)
		return;
	item = WMGetListItem(ch->list, ch->selectedRow)->clientData;
	domain = WMReadPropListFromFile(path);
	if (!domain || !WMIsPLDictionary(domain)) {
		if (domain)
			WMReleasePropList(domain);
		domain = WMCreatePLDictionary(NULL, NULL);
	}
	texture = makeTexture(ch, item->path);

	if (applyToAllMonitors(ch)) {
		int workspace = currentWorkspace(WMScreenDisplay(ch->screen));

		storeDefault(ch, domain, "WorkspaceBack", texture);
		value = WMCreatePLDictionary(NULL, NULL);
		storeDefault(ch, domain, "MonitorSpecificBack", value);
		WMReleasePropList(value);

		/* A workspace-specific texture would hide the new wallpaper on the current workspace. */
		value = domainValue(domain, "WorkspaceSpecificBack");
		if (value && WMIsPLArray(value) && workspace >= 0 && workspace < WMGetPropListItemCount(value)) {
			WMPropList *entry = WMGetFromPLArray(value, workspace);

			if (WMIsPLArray(entry) && WMGetPropListItemCount(entry) > 0) {
				value = WMDeepCopyPropList(value);
				WMDeleteFromPLArray(value, workspace);
				WMInsertInPLArray(value, workspace, texture);
				storeDefault(ch, domain, "WorkspaceSpecificBack", value);
				WMReleasePropList(value);
			}
		}
		text = formatString(_("Wallpaper set for all monitors."));
	} else {
		WallpaperMonitor *m = &ch->monitors[ch->currentMonitor];
		WMPropList *name = WMCreatePLString(m->name);

		value = domainValue(domain, "MonitorSpecificBack");
		if (value && WMIsPLDictionary(value))
			value = WMDeepCopyPropList(value);
		else
			value = WMCreatePLDictionary(NULL, NULL);
		WMPutInPLDictionary(value, name, texture);
		WMReleasePropList(name);
		storeDefault(ch, domain, "MonitorSpecificBack", value);
		WMReleasePropList(value);
		text = formatString(_("Wallpaper set for monitor %s."), m->name);
	}
	WMReleasePropList(texture);

	dir = wstrdup(path);
	if (strrchr(dir, '/'))
		*strrchr(dir, '/') = 0;
	if (!makeDirectories(dir) || !WMWritePropListToFile(domain, path)) {
		wfree(text);
		text = formatString(_("Could not save %s."), path);
	} else if (ch->activate) {
		ch->activate(ch, domain);
	}
	setStatus(ch, text);
	wfree(text);
	wfree(dir);
	wfree(path);
	WMReleasePropList(domain);
}

static Bool readMonitorLine(const char *line, WallpaperMonitor *monitor)
{
	int x, y, width, height, offset = 0;

	if (sscanf(line, "%d %d %d %d %n", &x, &y, &width, &height, &offset) != 4 || offset == 0 || width <= 0 ||
	    height <= 0)
		return False;
	monitor->rect = wmkrect(x, y, width, height);
	monitor->name = wstrdup(line + offset);
	if (strchr(monitor->name, '\n'))
		*strchr(monitor->name, '\n') = 0;
	return monitor->name[0] != 0;
}

/* Monitor names come from wmsetbg, which also matches them when drawing the background. */
static void detectMonitors(WMScreen *screen, WallpaperMonitor **monitors, int *count, int *current)
{
	Display *dpy = WMScreenDisplay(screen);
	FILE *pipe = popen("wmsetbg --list-monitors 2>/dev/null", "r");
	char line[512];
	Window root, child;
	int rx = 0, ry = 0, wx, wy, i;
	unsigned int mask;

	*monitors = NULL;
	*count = 0;
	*current = 0;
	while (pipe && fgets(line, sizeof(line), pipe)) {
		WallpaperMonitor monitor;

		if (readMonitorLine(line, &monitor)) {
			*monitors = wrealloc(*monitors, sizeof(WallpaperMonitor) * (*count + 1));
			(*monitors)[(*count)++] = monitor;
		}
	}
	if (pipe)
		pclose(pipe);
	if (*count == 0) {
		*monitors = wmalloc(sizeof(WallpaperMonitor));
		(*monitors)[0].rect = wmkrect(0, 0, WMScreenWidth(screen), WMScreenHeight(screen));
		(*monitors)[0].name = NULL;
		*count = 1;
		return;
	}
	XQueryPointer(dpy, DefaultRootWindow(dpy), &root, &child, &rx, &ry, &wx, &wy, &mask);
	for (i = 0; i < *count; i++) {
		WMRect r = (*monitors)[i].rect;

		if (rx >= r.pos.x && ry >= r.pos.y && rx < r.pos.x + (int)r.size.width &&
		    ry < r.pos.y + (int)r.size.height) {
			*current = i;
			break;
		}
	}
}

/* Start with the texture currently configured for the target monitor. */
static void loadCurrentSettings(WallpaperChooser *ch)
{
	char *path = wdefaultspathfordomain("WindowMaker");
	WMPropList *domain = WMReadPropListFromFile(path);
	WMPropList *texture = NULL, *overrides;
	const char *name = ch->monitors[ch->currentMonitor].name;
	int i;

	wfree(path);
	if (!domain)
		return;
	if (WMIsPLDictionary(domain)) {
		overrides = domainValue(domain, "MonitorSpecificBack");
		if (name && overrides && WMIsPLDictionary(overrides)) {
			WMPropList *key = WMCreatePLString(name);

			texture = WMGetFromPLDictionary(overrides, key);
			WMReleasePropList(key);
		}
		if (texture && ch->thisMonitorB) {
			WMPerformButtonClick(ch->thisMonitorB);
		} else if (!texture) {
			texture = domainValue(domain, "WorkspaceBack");
		}
	}
	if (texture && WMIsPLArray(texture) && WMGetPropListItemCount(texture) >= 2 &&
	    WMIsPLString(WMGetFromPLArray(texture, 0)) && WMIsPLString(WMGetFromPLArray(texture, 1))) {
		const char *type = WMGetFromPLString(WMGetFromPLArray(texture, 0));

		for (i = 0; i < MODE_COUNT; i++) {
			if (strcasecmp(type, wallpaperModes[i].type) == 0) {
				WMPerformButtonClick(ch->modeB[i]);
				ch->selectPath = wexpandpath(WMGetFromPLString(WMGetFromPLArray(texture, 1)));
				break;
			}
		}
	}
	WMReleasePropList(domain);
}

static WMBox *createRow(WMBox *parent)
{
	WMBox *row = WMCreateBox(parent);

	WMSetBoxHorizontal(row, True);
	return row;
}

static WMLabel *createLabel(WMWidget *parent, const char *text)
{
	WMLabel *label = WMCreateLabel(parent);

	WMSetLabelText(label, text);
	WMSetLabelTextAlignment(label, WALeft);
	return label;
}

static void packRow(WMBox *parent, WMBox *row, int space)
{
	WMPackBoxSubview(parent, row, WMGetBoxMinimumSize(row), False, space);
	WMMapSubwidgets(row);
}

static void computeSizes(WallpaperChooser *ch)
{
	int em = WMWidthOfString(ch->font, "M", 1), fh = WMFontHeight(ch->font);
	int maxWidth = 24 * em, maxHeight = 15 * em;
	WMRect d = ch->desktop;

	ch->thumbHeight = WMAX(40, 3 * fh);
	ch->thumbWidth = ch->thumbHeight * 16 / 10;
	ch->previewWidth = maxWidth;
	ch->previewHeight = (int)((long long)maxWidth * d.size.height / d.size.width);
	if (ch->previewHeight > maxHeight) {
		ch->previewHeight = maxHeight;
		ch->previewWidth = (int)((long long)maxHeight * d.size.width / d.size.height);
	}
	ch->previewWidth = WMAX(1, ch->previewWidth);
	ch->previewHeight = WMAX(1, ch->previewHeight);
}

static WallpaperChooser *createChooser(WMWidget *parent, WallpaperMonitor *monitors, int count, int current)
{
	WallpaperChooser *ch = wmalloc(sizeof(*ch));
	WMScreen *screen = WMWidgetScreen(parent);
	WMBox *row, *main, *column;
	WMLabel *label;
	WMSize size;
	int i, fh, em, gap, x0, y0, x1, y1;
	char *text, *monitor;

	ch->screen = screen;
	ch->parent = parent;
	ch->monitors = monitors;
	ch->monitorCount = count;
	ch->currentMonitor = current;
	ch->selectedRow = -1;
	ch->activate = activateBackground;
	ch->cacheDir = thumbnailCacheDir();
	ch->font = WMDefaultSystemFont(screen);
	ch->black = WMBlackColor(screen);
	ch->white = WMWhiteColor(screen);
	ch->gray = WMGrayColor(screen);
	ch->darkGray = WMDarkGrayColor(screen);
	fh = WMFontHeight(ch->font);
	em = WMWidthOfString(ch->font, "M", 1);
	gap = WMAX(6, fh / 2);

	x0 = y0 = INT_MAX;
	x1 = y1 = INT_MIN;
	for (i = 0; i < count; i++) {
		WMRect r = monitors[i].rect;

		x0 = WMIN(x0, r.pos.x);
		y0 = WMIN(y0, r.pos.y);
		x1 = WMAX(x1, r.pos.x + (int)r.size.width);
		y1 = WMAX(y1, r.pos.y + (int)r.size.height);
	}
	ch->desktop = wmkrect(x0, y0, x1 - x0, y1 - y0);
	computeSizes(ch);

	ch->box = WMCreateBox(parent);

	row = createRow(ch->box);
	label = createLabel(row, _("Folder:"));
	WPrefsPackLabel(row, label, gap);
	ch->folderT = WMCreateTextField(row);
	WMPackBoxSubview(row, ch->folderT, WMGetTextFieldPreferredSize(ch->folderT, 30), True, gap);
	ch->browseB = WMCreateCommandButton(row);
	WMSetButtonText(ch->browseB, _("Browse..."));
	WMSetButtonAction(ch->browseB, browseFolder, ch);
	WPrefsPackButton(row, ch->browseB, 0);
	packRow(ch->box, row, gap);
	WMAddNotificationObserver(folderEdited, ch, WMTextDidEndEditingNotification, ch->folderT);

	row = createRow(ch->box);
	label = createLabel(row, _("Placement:"));
	WPrefsPackLabel(row, label, gap);
	for (i = 0; i < MODE_COUNT; i++) {
		ch->modeB[i] = WMCreateRadioButton(row);
		WMSetButtonText(ch->modeB[i], _(wallpaperModes[i].label));
		WMSetButtonAction(ch->modeB[i], optionChanged, ch);
		if (i > 0)
			WMGroupButtons(ch->modeB[0], ch->modeB[i]);
		WPrefsPackButton(row, ch->modeB[i], gap);
	}
	WMSetButtonSelected(ch->modeB[MODE_SCALE], True);
	packRow(ch->box, row, gap);

	if (count > 1) {
		row = createRow(ch->box);
		label = createLabel(row, _("Apply to:"));
		WPrefsPackLabel(row, label, gap);
		ch->thisMonitorB = WMCreateRadioButton(row);
		monitor = monitorDescription(ch, current);
		text = formatString(_("This monitor: %s"), monitor);
		WMSetButtonText(ch->thisMonitorB, text);
		wfree(text);
		wfree(monitor);
		WMSetButtonAction(ch->thisMonitorB, optionChanged, ch);
		WPrefsPackButton(row, ch->thisMonitorB, gap);
		ch->allMonitorsB = WMCreateRadioButton(row);
		text = formatString(_("All %d monitors"), count);
		WMSetButtonText(ch->allMonitorsB, text);
		wfree(text);
		WMSetButtonAction(ch->allMonitorsB, optionChanged, ch);
		WMGroupButtons(ch->thisMonitorB, ch->allMonitorsB);
		WMSetButtonSelected(ch->allMonitorsB, True);
		WPrefsPackButton(row, ch->allMonitorsB, 0);
		packRow(ch->box, row, gap);
	}

	main = createRow(ch->box);
	ch->list = WMCreateList(main);
	WMSetListUserDrawProc(ch->list, paintItem);
	WMSetListUserDrawItemHeight(ch->list, ch->thumbHeight + WMAX(6, fh / 2));
	WMHangData(ch->list, ch);
	WMSetListAction(ch->list, listSelected, ch);
	WMSetListDoubleAction(ch->list, applyWallpaper, ch);
	WMPackBoxSubview(main, ch->list,
			 wmksize(ch->thumbWidth + 16 * em + WMGetScrollerWidth(WMGetListScroller(ch->list)),
				 WMAX(ch->previewHeight + 3 * fh, 3 * (ch->thumbHeight + fh / 2))),
			 True, gap);
	column = WMCreateBox(main);
	ch->previewL = WMCreateLabel(column);
	WMSetLabelImagePosition(ch->previewL, WIPImageOnly);
	WMSetLabelRelief(ch->previewL, WRSunken);
	WMPackBoxSubview(column, ch->previewL, wmksize(ch->previewWidth + 4, ch->previewHeight + 4), False, gap / 2);
	ch->captionL = WMCreateLabel(column);
	WMSetLabelWraps(ch->captionL, True);
	WMSetLabelTextAlignment(ch->captionL, WACenter);
	WMPackBoxSubview(column, ch->captionL, wmksize(ch->previewWidth + 4, 2 * fh + 4), False, 0);
	size = WMGetBoxMinimumSize(column);
	WMPackBoxSubview(main, column, size, False, 0);
	WMMapSubwidgets(column);
	WMPackBoxSubview(ch->box, main, WMGetBoxMinimumSize(main), True, gap);
	WMMapSubwidgets(main);

	row = createRow(ch->box);
	ch->progress = WMCreateProgressIndicator(row);
	WMPackBoxSubview(row, ch->progress, wmksize(10 * em, fh), False, gap);
	ch->statusL = createLabel(row, "");
	WMPackBoxSubview(row, ch->statusL, wmksize(16 * em, fh + 4), True, gap);
	ch->applyB = WMCreateCommandButton(row);
	WMSetButtonText(ch->applyB, _("Apply"));
	WMSetButtonAction(ch->applyB, applyWallpaper, ch);
	WMSetButtonEnabled(ch->applyB, False);
	WPrefsPackButton(row, ch->applyB, 0);
	packRow(ch->box, row, 0);
	WMUnmapWidget(ch->progress);

	loadCurrentSettings(ch);
	updatePreview(ch);
	return ch;
}

WallpaperChooser *WallpaperChooserCreate(WMWidget *parent)
{
	WallpaperMonitor *monitors;
	int count, current;

	detectMonitors(WMWidgetScreen(parent), &monitors, &count, &current);
	return createChooser(parent, monitors, count, current);
}

void WallpaperChooserDestroy(WallpaperChooser *ch)
{
	int i;

	WMRemoveNotificationObserver(ch);
	freeItems(ch);
	clearSelection(ch);
	if (ch->desktopImage)
		RReleaseImage(ch->desktopImage);
	for (i = 0; i < ch->monitorCount; i++)
		if (ch->monitors[i].name)
			wfree(ch->monitors[i].name);
	wfree(ch->monitors);
	if (ch->folder)
		wfree(ch->folder);
	if (ch->selectPath)
		wfree(ch->selectPath);
	wfree(ch->cacheDir);
	WMReleaseFont(ch->font);
	WMReleaseColor(ch->black);
	WMReleaseColor(ch->white);
	WMReleaseColor(ch->gray);
	WMReleaseColor(ch->darkGray);
	wfree(ch);
}

WMBox *WallpaperChooserGetBox(WallpaperChooser *ch)
{
	return ch->box;
}

WMSize WallpaperChooserGetMinimumSize(WallpaperChooser *ch)
{
	return WMGetBoxMinimumSize(ch->box);
}

void WallpaperChooserSetApplyHook(WallpaperChooser *ch, WallpaperChooserApplyHook *hook, void *data)
{
	ch->hook = hook;
	ch->hookData = data;
}
