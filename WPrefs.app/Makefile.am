SUBDIRS = xpm tiff po

AUTOMAKE_OPTIONS =

wpexecbindir = @wprefs_bindir@

wpexecbin_PROGRAMS = WPrefs

bin_PROGRAMS = wmwallpaper

wpdatadir = @wprefs_datadir@

wpdata_DATA = WPrefs.tiff WPrefs.xpm

applicationsdir = @datadir@/applications

applications_DATA = WPrefs.desktop

WPrefs.desktop: WPrefs.desktop.in
	$(AM_V_GEN)sed -e "s:#wprefs_bindir#:${wprefs_bindir}:" \
		       -e "s:#wprefs_datadir#:${wprefs_datadir}:" $< > $@

EXTRA_DIST = $(wpdata_DATA) \
	KeyboardSettings.c \
	Themes.c \
	WPrefs.desktop.in

DISTCLEANFILES = WPrefs.desktop

WPrefs_SOURCES = \
	main.c \
	WPrefs.c \
	WPrefs.h \
	Appearance.c \
	Configurations.c \
	Docks.c \
	Expert.c \
	Focus.c \
	FontSimple.c \
	FontControls.c \
	FontControls.h \
	HotCornerShortcuts.c \
	Icons.c \
	KeyboardShortcuts.c \
	Menu.c \
	MenuPreferences.c \
	MouseSettings.c \
	Paths.c \
	PanelLayout.c \
	PanelLayout.h \
	Preferences.c \
	TexturePanel.c \
	TexturePanel.h \
	Wallpaper.c \
	WallpaperChooser.c \
	WallpaperChooser.h \
	WindowHandling.c \
	Workspace.c \
	double.c \
	editmenu.c \
	editmenu.h \
	xmodifier.c

# not_yet_fully_implemented
#WPrefs_SOURCES += \
#	KeyboardSettings.c \
#	Themes.c

AM_CFLAGS = @PANGO_CFLAGS@

AM_CPPFLAGS = -DRESOURCE_PATH=\"$(wpdatadir)\" -DWMAKER_RESOURCE_PATH=\"$(pkgdatadir)\" \
	 -I$(top_builddir)/wrlib -I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	 @HEADER_SEARCH_PATH@

WPrefs_DEPENDENCIES = $(top_builddir)/WINGs/libWINGs.la

WPrefs_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la\
	$(top_builddir)/WINGs/libWUtil.la\
	$(top_builddir)/wrlib/libwraster.la \
	@XLFLAGS@ @XLIBS@ \
	@LIBM@ \
	@FCLIBS@ \
	@INTLIBS@

wmwallpaper_SOURCES = wmwallpaper.c WallpaperChooser.c WallpaperChooser.h PanelLayout.c PanelLayout.h
wmwallpaper_DEPENDENCIES = $(WPrefs_DEPENDENCIES)
wmwallpaper_LDADD = $(WPrefs_LDADD)

check_PROGRAMS = font_controls_test window_handling_test icons_test paths_test configurations_test keyboard_shortcuts_test hot_corner_test mouse_settings_test expert_test font_simple_test menu_test appearance_test wallpaper_test
wallpaper_test_SOURCES = wallpaper_test.c PanelLayout.c PanelLayout.h WallpaperChooser.h
wallpaper_test_LDADD = $(WPrefs_LDADD)
appearance_test_SOURCES = appearance_test.c FontControls.c FontControls.h PanelLayout.c PanelLayout.h
appearance_test_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_IMAGE_PATH=\"$(abs_srcdir)/tiff\"
appearance_test_LDADD = $(WPrefs_LDADD)
menu_test_SOURCES = menu_test.c editmenu.h xmodifier.c PanelLayout.c PanelLayout.h
menu_test_LDADD = $(WPrefs_LDADD)
font_simple_test_SOURCES = font_simple_test.c PanelLayout.c PanelLayout.h
font_simple_test_LDADD = $(WPrefs_LDADD)
font_controls_test_SOURCES = font_controls_test.c FontControls.c FontControls.h PanelLayout.c PanelLayout.h
font_controls_test_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_IMAGE_PATH=\"$(abs_srcdir)/tiff\"
font_controls_test_LDADD = $(WPrefs_LDADD)
window_handling_test_SOURCES = window_handling_test.c FontControls.c FontControls.h PanelLayout.c PanelLayout.h
window_handling_test_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_IMAGE_PATH=\"$(abs_srcdir)/tiff\"
window_handling_test_LDADD = $(WPrefs_LDADD)
icons_test_SOURCES = icons_test.c FontControls.c FontControls.h PanelLayout.c PanelLayout.h
icons_test_CPPFLAGS = $(AM_CPPFLAGS)
icons_test_LDADD = $(WPrefs_LDADD)
paths_test_SOURCES = paths_test.c PanelLayout.c PanelLayout.h
paths_test_CPPFLAGS = $(AM_CPPFLAGS)
paths_test_LDADD = $(WPrefs_LDADD)
configurations_test_SOURCES = configurations_test.c FontControls.c FontControls.h PanelLayout.c PanelLayout.h
configurations_test_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_IMAGE_PATH=\"$(abs_srcdir)/tiff\"
configurations_test_LDADD = $(WPrefs_LDADD)
keyboard_shortcuts_test_SOURCES = keyboard_shortcuts_test.c PanelLayout.c PanelLayout.h
keyboard_shortcuts_test_CPPFLAGS = $(AM_CPPFLAGS)
keyboard_shortcuts_test_LDADD = $(WPrefs_LDADD)
hot_corner_test_SOURCES = hot_corner_test.c FontControls.c FontControls.h PanelLayout.c PanelLayout.h
hot_corner_test_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_IMAGE_PATH=\"$(abs_srcdir)/tiff\"
hot_corner_test_LDADD = $(WPrefs_LDADD)
mouse_settings_test_SOURCES = mouse_settings_test.c double.c xmodifier.c FontControls.c FontControls.h PanelLayout.c PanelLayout.h
mouse_settings_test_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_IMAGE_PATH=\"$(abs_srcdir)/tiff\"
mouse_settings_test_LDADD = $(WPrefs_LDADD)
expert_test_SOURCES = expert_test.c FontControls.c FontControls.h PanelLayout.c PanelLayout.h
expert_test_CPPFLAGS = $(AM_CPPFLAGS)
expert_test_LDADD = $(WPrefs_LDADD)

check-local: $(check_PROGRAMS)
	./font_controls_test || test $$? -eq 77
	./window_handling_test || test $$? -eq 77
	./icons_test || test $$? -eq 77
	./paths_test || test $$? -eq 77
	./configurations_test || test $$? -eq 77
	./keyboard_shortcuts_test || test $$? -eq 77
	./hot_corner_test || test $$? -eq 77
	./mouse_settings_test || test $$? -eq 77
	./expert_test || test $$? -eq 77
	./font_simple_test || test $$? -eq 77
	./menu_test || test $$? -eq 77
	./appearance_test || test $$? -eq 77
	./wallpaper_test || test $$? -eq 77
