#include "wpopupbutton.c"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static int actions;

static void changed(WMWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	actions++;
}

static WMPixmap *testIcon(WMScreen *screen, int width, int height, Bool blue)
{
	RImage *image = RCreateImage(width, height, True);
	WMPixmap *pixmap;

	assert(image);
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++) {
			unsigned char *pixel = image->data + 4 * (y * width + x);

			pixel[0] = blue ? 0 : 255;
			pixel[1] = 0;
			pixel[2] = blue ? 255 : 0;
			pixel[3] = x >= 2 && x < width - 2 && y >= 2 && y < height - 2 ? 255 : 0;
		}
	pixmap = WMCreatePixmapFromRImage(screen, image, 127);
	RReleaseImage(image);
	assert(pixmap);
	return pixmap;
}

static int coloredPixels(WMScreen *screen, Drawable drawable, int row, int rowHeight, Bool blue)
{
	RImage *image = RCreateImageFromDrawable(screen->rcontext, drawable, None);
	int count = 0, stride;

	assert(image);
	stride = image->format == RRGBAFormat ? 4 : 3;
	for (int y = row * rowHeight; y < (row + 1) * rowHeight; y++)
		for (unsigned int x = 0; x < image->width; x++) {
			unsigned char *pixel = image->data + stride * (y * image->width + x);

			if (pixel[blue ? 2 : 0] > 200 && pixel[1] < 50 && pixel[blue ? 0 : 2] < 50)
				count++;
		}
	RReleaseImage(image);
	return count;
}

static void testClippedText(WMScreen *screen, PopUpButton *popup)
{
	Pixmap drawable;
	RImage *text, *blank;
	int extra = 2 * (W_ButtonBorderWidth(popup->font) - 1);
	int left = 6 + extra + 18 + 6;
	int right = popup->view->size.width - popup->indicator->width - 8 - extra;
	int changed = 0, stride;

	WMSetMenuItemTitle(WMGetPopUpButtonMenuItem(popup, 0), "Long application label that must not cover the arrow");
	drawable = makeMenuPixmap(popup);
	text = RCreateImageFromDrawable(screen->rcontext, drawable, None);
	XFreePixmap(screen->display, drawable);
	WMSetMenuItemTitle(WMGetPopUpButtonMenuItem(popup, 0), "");
	drawable = makeMenuPixmap(popup);
	blank = RCreateImageFromDrawable(screen->rcontext, drawable, None);
	XFreePixmap(screen->display, drawable);
	assert(text && blank);
	stride = text->format == RRGBAFormat ? 4 : 3;
	for (unsigned int y = 0; y < popup->view->size.height; y++)
		for (unsigned int x = 0; x < text->width; x++) {
			int offset = stride * (y * text->width + x);
			int different = memcmp(text->data + offset, blank->data + offset, 3) != 0;

			if (x < left || x >= right)
				assert(!different);
			else
				changed += different;
		}
	assert(changed > 0);
	RReleaseImage(text);
	RReleaseImage(blank);
	WMSetMenuItemTitle(WMGetPopUpButtonMenuItem(popup, 0), "First");
}

static void testPopup(WMScreen *screen, int fontSize)
{
	WMFont *font = WMSystemFontOfSize(screen, fontSize);
	WMWindow *window = WMCreateWindow(screen, "popupbuttonTest");
	PopUpButton *popup = WMCreatePopUpButton(window);
	WMPixmap *red = testIcon(screen, 18, 12, False);
	WMPixmap *blue;
	WMSize plain, withIcons, restored;
	Pixmap menu;
	int height;

	WMSetPopUpButtonFont(popup, font);
	WMReleaseFont(font);
	WMAddPopUpButtonItem(popup, "First");
	WMAddPopUpButtonItem(popup, "Text only");
	WMAddPopUpButtonItem(popup, "Disabled");
	WMSetPopUpButtonSelectedItem(popup, 0);
	WMSetPopUpButtonItemEnabled(popup, 2, False);
	plain = WMGetPopUpButtonPreferredSize(popup);
	WMSetPopUpButtonItemPixmap(popup, 0, red);
	WMSetPopUpButtonItemPixmap(popup, 2, red);
	WMReleasePixmap(red);
	withIcons = WMGetPopUpButtonPreferredSize(popup);
	assert(withIcons.width == plain.width + 24);
	assert(withIcons.height >= plain.height);
	height = withIcons.height;
	WMResizeWidget(window, withIcons.width + 20, height * 4 + 20);
	WMResizeWidget(popup, withIcons.width, height);
	WMRealizeWidget(window);
	WMMapSubwidgets(window);
	WMMapWidget(window);
	paintPopUpButton(popup);
	/* Transparent icon borders must not become opaque colored rectangles. */
	assert(coloredPixels(screen, popup->view->window, 0, height, False) == 112);
	menu = makeMenuPixmap(popup);
	assert(coloredPixels(screen, menu, 0, height, False) == 112);
	assert(coloredPixels(screen, menu, 1, height, False) == 0);
	assert(coloredPixels(screen, menu, 2, height, False) == 112);
	XFreePixmap(screen->display, menu);
	popUpMenu(popup);
	paintMenuEntry(popup, 0, True);
	assert(coloredPixels(screen, popup->menuView->window, 0, height, False) == 112);
	blue = testIcon(screen, 18, 12, True);
	WMSetPopUpButtonItemPixmap(popup, 0, blue);
	WMReleasePixmap(blue);
	assert(coloredPixels(screen, popup->menuView->window, 0, height, True) == 112);
	assert(coloredPixels(screen, popup->menuView->window, 0, height, False) == 0);
	WMSetPopUpButtonItemPixmap(popup, 0, blue);
	assert(WMGetMenuItemPixmap(WMGetPopUpButtonMenuItem(popup, 0)) == blue);
	popDownMenu(popup);
	paintPopUpButton(popup);
	assert(coloredPixels(screen, popup->view->window, 0, height, True) == 112);
	actions = 0;
	WMSetPopUpButtonAction(popup, changed, NULL);
	WMSelectPopUpButtonNextItem(popup);
	assert(WMGetPopUpButtonSelectedItem(popup) == 1 && actions == 1);
	WMSelectPopUpButtonNextItem(popup);
	assert(WMGetPopUpButtonSelectedItem(popup) == 1 && actions == 1);
	WMSelectPopUpButtonPreviousItem(popup);
	assert(WMGetPopUpButtonSelectedItem(popup) == 0 && actions == 2);
	WMResizeWidget(popup, 120, height);
	testClippedText(screen, popup);
	WMResizeWidget(popup, 120, height + 8);
	popUpMenu(popup);
	assert(popup->menuView->size.height == 3 * (height + 8));
	popDownMenu(popup);
	WMSetPopUpButtonItemPixmap(popup, 0, NULL);
	WMSetPopUpButtonItemPixmap(popup, 2, NULL);
	restored = WMGetPopUpButtonPreferredSize(popup);
	assert(restored.width == plain.width && restored.height == plain.height);
	red = testIcon(screen, 80, 80, False);
	WMSetPopUpButtonItemPixmap(popup, 0, red);
	WMReleasePixmap(red);
	WMResizeWidget(popup, 60, 20);
	popUpMenu(popup);
	paintMenuEntry(popup, 0, True);
	assert(coloredPixels(screen, popup->menuView->window, 0, 20, False) > 0);
	assert(coloredPixels(screen, popup->menuView->window, 1, 20, False) == 0);
	popDownMenu(popup);
	WMSetPopUpButtonPullsDown(popup, True);
	WMSetPopUpButtonText(popup, "Actions");
	paintPopUpButton(popup);
	assert(coloredPixels(screen, popup->view->window, 0, 20, False) == 0);
	menu = makeMenuPixmap(popup);
	assert(coloredPixels(screen, menu, 0, 20, False) > 0);
	XFreePixmap(screen->display, menu);
	WMSetPopUpButtonPullsDown(popup, False);
	WMSetPopUpButtonSelectedItem(popup, 1);
	WMRemovePopUpButtonItem(popup, 0);
	assert(menuImageSize(popup).width == 0);
	assert(WMGetPopUpButtonSelectedItem(popup) == 0);
	WMRemovePopUpButtonItem(popup, 0);
	WMRemovePopUpButtonItem(popup, 0);
	assert(WMGetPopUpButtonSelectedItem(popup) == -1);
	WMDestroyWidget(window);
}

int main(int argc, char **argv)
{
	Display *display;
	WMScreen *screen;

	if (!getenv("DISPLAY")) {
		puts("SKIP: popupbutton_test needs an X server");
		return 77;
	}
	WMInitializeApplication("popupbutton_test", &argc, argv);
	display = XOpenDisplay(NULL);
	assert(display);
	screen = WMCreateScreen(display, DefaultScreen(display));
	assert(screen);
	testPopup(screen, 11);
	testPopup(screen, 24);
	XSync(display, False);
	WMReleaseApplication();
	XCloseDisplay(display);
	puts("Icon/text popup rendering, transparency, clipping and selection tests passed");
	return 0;
}
