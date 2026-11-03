/* Run with DISPLAY pointing to an isolated test X server. */
#define main wmsetbg_main
#include "wmsetbg.c"
#undef main

#undef NDEBUG
#include <assert.h>
#include <X11/Xutil.h>
#include <sys/wait.h>

static void send_message(int fd, const char *message)
{
	char size[5];
	int length = strlen(message);

	snprintf(size, sizeof(size), "%4d", length);
	assert(write(fd, size, 4) == 4);
	assert(write(fd, message, length) == length);
}

static void wait_background_size(unsigned int expected_width, unsigned int expected_height,
                                 const char *expected_color)
{
	Atom property = XInternAtom(dpy, "_XROOTPMAP_ID", False);
	XColor color;
	int attempt;

	if (expected_color)
		assert(XAllocNamedColor(dpy, DefaultColormap(dpy, scr), expected_color, &color, &color));
	for (attempt = 0; attempt < 100; attempt++) {
		Atom type;
		int format, x, y;
		unsigned long count, remaining;
		unsigned char *data = NULL;
		unsigned int width = 0, height = 0, border, depth;
		Window pixmap_root;
		Bool color_matches = True;

		XGrabServer(dpy);
		assert(XGetWindowProperty(dpy, root, property, 0, 1, False, XA_PIXMAP,
		                          &type, &format, &count, &remaining, &data) == Success);
		if (type == XA_PIXMAP && format == 32 && count == 1)
			assert(XGetGeometry(dpy, *(Pixmap *)data, &pixmap_root, &x, &y,
			                    &width, &height, &border, &depth));
		if (expected_color && width == expected_width && height == expected_height) {
			XImage *image = XGetImage(dpy, *(Pixmap *)data, 0, 0,
			                         width, height, AllPlanes, ZPixmap);

			assert(image);
			color_matches = XGetPixel(image, 0, 0) == color.pixel &&
			                XGetPixel(image, width - 1, height - 1) == color.pixel;
			XDestroyImage(image);
		}
		if (data)
			XFree(data);
		XUngrabServer(dpy);
		XSync(dpy, False);
		if (width == expected_width && height == expected_height && color_matches)
			return;
		usleep(10000);
	}
	assert(!"background helper did not publish the expected pixmap size");
}

int main(int argc, char **argv)
{
	RContext *rc;
	BackgroundTexture *textures[WORKSPACE_COUNT] = { NULL };
	XWindowAttributes attrs;
	Window pixmap_root;
	int x, y, maxTextures = 0;
	unsigned int width, height, border, depth;

	if (!getenv("DISPLAY")) {
		puts("SKIP: DISPLAY must point to an isolated test X server");
		return 77;
	}
	dpy = XOpenDisplay(NULL);
	assert(dpy);
	scr = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);
	assert(XGetWindowAttributes(dpy, root, &attrs));
	assert(attrs.width > 1 && attrs.height > 1);
	rc = RCreateContext(dpy, scr, NULL);
	assert(rc);

	/* Simulate textures cached before the framebuffer grew. */
	scrWidth = attrs.width / 2;
	scrHeight = attrs.height / 2;
	if (argc == 2 && strcmp(argv[1], "--helper") == 0) {
		alarm(10);
		helperLoop(rc);
	}
	setupTexture(rc, textures, &maxTextures, 0, "(dgradient, red, blue)");
	setupTexture(rc, textures, &maxTextures, 3, "(solid, green)");
	setupTexture(rc, textures, &maxTextures, 2, "(dgradient, red, blue)");
	assert(textures[0] && textures[0] == textures[2]);
	assert(textures[0]->width == scrWidth);
	assert(textures[0]->refcount == 2);

	refreshTextures(rc, textures, maxTextures);
	assert(scrWidth == attrs.width && scrHeight == attrs.height);
	assert(textures[0] == textures[2] && textures[0]->refcount == 2);
	assert(textures[1] == NULL);
	assert(textures[0]->width == attrs.width);
	assert(textures[0]->height == attrs.height);
	assert(textures[3]->solid);
	assert(XGetGeometry(dpy, textures[0]->pixmap, &pixmap_root, &x, &y,
	                    &width, &height, &border, &depth));
	assert(width == (unsigned int)attrs.width && height == (unsigned int)attrs.height);

	/* Small textures must be published tiled over the whole screen. */
	{
		BackgroundTexture tile = { 0 };
		Pixmap published;
		XImage *image;
		int cx = attrs.width - 1, cy = attrs.height - 1;

		tile.pixmap = XCreatePixmap(dpy, root, 2, 2, DefaultDepth(dpy, scr));
		tile.width = tile.height = 2;
		XSetForeground(dpy, DefaultGC(dpy, scr), BlackPixel(dpy, scr));
		XFillRectangle(dpy, tile.pixmap, DefaultGC(dpy, scr), 0, 0, 2, 2);
		XSetForeground(dpy, DefaultGC(dpy, scr), WhitePixel(dpy, scr));
		XDrawPoint(dpy, tile.pixmap, DefaultGC(dpy, scr), 1, 1);
		changeTexture(&tile);
		wait_background_size(attrs.width, attrs.height, NULL);
		assert(getRootPixmapAtom(XInternAtom(dpy, "_XROOTPMAP_ID", False), &published));
		image = XGetImage(dpy, published, 0, 0, attrs.width, attrs.height,
		                  AllPlanes, ZPixmap);
		assert(image);
		assert(XGetPixel(image, 0, 0) == BlackPixel(dpy, scr));
		assert(XGetPixel(image, 1, 1) == WhitePixel(dpy, scr));
		assert(XGetPixel(image, cx, cy) ==
		       ((cx & cy & 1) ? WhitePixel(dpy, scr) : BlackPixel(dpy, scr)));
		XDestroyImage(image);
		XFreePixmap(dpy, tile.pixmap);
	}

	/* A second resize must invalidate the cache again, including shared entries. */
	scrWidth = 1;
	scrHeight = 1;
	refreshTextures(rc, textures, maxTextures);
	assert(textures[0] == textures[2] && textures[0]->refcount == 2);
	assert(textures[0]->width == attrs.width && textures[0]->height == attrs.height);
	setupTexture(rc, textures, &maxTextures, 0, NULL);
	assert(textures[2]->refcount == 1);
	setupTexture(rc, textures, &maxTextures, 2, NULL);
	setupTexture(rc, textures, &maxTextures, 3, NULL);

	/* Exercise the refresh protocol, workspace selection and default fallback. */
	{
		int pipe_fd[2], status;
		pid_t child;

		assert(pipe(pipe_fd) == 0);
		child = fork();
		assert(child >= 0);
		if (child == 0) {
			close(pipe_fd[1]);
			assert(dup2(pipe_fd[0], STDIN_FILENO) >= 0);
			close(pipe_fd[0]);
			execl(argv[0], argv[0], "--helper", NULL);
			_exit(1);
		}
		close(pipe_fd[0]);
		send_message(pipe_fd[1], "S   0(dgradient, red, blue)");
		send_message(pipe_fd[1], "S   3(solid, green)");
		send_message(pipe_fd[1], "S   2(dgradient, red, blue)");
		send_message(pipe_fd[1], "C   2");
		wait_background_size(attrs.width / 2, attrs.height / 2, NULL);
		send_message(pipe_fd[1], "R   2");
		wait_background_size(attrs.width, attrs.height, NULL);
		send_message(pipe_fd[1], "C   3");
		wait_background_size(attrs.width, attrs.height, "green");
		send_message(pipe_fd[1], "R   1");
		wait_background_size(attrs.width, attrs.height, NULL);
		send_message(pipe_fd[1], "K");
		close(pipe_fd[1]);
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}

	if (xineInfo.screens)
		wfree(xineInfo.screens);
	RDestroyContext(rc);
	XCloseDisplay(dpy);
	puts("Background resize and shared workspace cache tests passed");
	return 0;
}
