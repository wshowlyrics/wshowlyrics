project(
	'lyrics',
	'c',
	version: '0.1.0',
	license: 'GPL',
	meson_version: '>=0.48.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
	'-D_POSIX_C_SOURCE=200809L',

	'-Wundef',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wstrict-prototypes',
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

cairo          = dependency('cairo')
fontconfig     = dependency('fontconfig')
pango          = dependency('pango')
pangocairo     = dependency('pangocairo')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols')
libcurl        = dependency('libcurl')
appindicator   = dependency('appindicator3-0.1')
gdkpixbuf      = dependency('gdk-pixbuf-2.0')

rt = cc.find_library('rt')

subdir('protocols')

executable(
	'lyrics',
	files(
		'main.c',
		'pango.c',
		'shm.c',
		'mpris.c',
		'lrc_parser.c',
		'srt_parser.c',
		'lrcx_parser.c',
		'lyrics_provider.c',
		'lrclib_provider.c',
		'system_tray.c',
		'config.c',
	),
	dependencies: [
		appindicator,
		cairo,
		client_protos,
		fontconfig,
		gdkpixbuf,
		libcurl,
		pango,
		pangocairo,
		rt,
		wayland_client,
		wayland_protos,
	],
	install: true,
)

# Install system-wide configuration file
install_data(
	'settings.ini.example',
	install_dir: '/etc/wshowlyrics',
	rename: 'settings.ini',
)
