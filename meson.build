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
openssl        = dependency('openssl')

rt = cc.find_library('rt')

subdir('protocols')

executable(
	'lyrics',
	files(
		'src/main.c',
		'src/utils/pango/pango_utils.c',
		'src/utils/shm/shm.c',
		'src/utils/mpris/mpris.c',
		'src/parser/lrc/lrc_parser.c',
		'src/parser/srt/srt_parser.c',
		'src/parser/lrcx/lrcx_parser.c',
		'src/provider/lyrics/lyrics_provider.c',
		'src/provider/lrclib/lrclib_provider.c',
		'src/provider/itunes/itunes_artwork.c',
		'src/user_experience/system_tray/system_tray.c',
		'src/user_experience/config/config.c',
		'src/parser/utils/parser_utils.c',
		'src/utils/curl/curl_utils.c',
		'src/utils/file/file_utils.c',
		'src/utils/json/json_utils.c',
		'src/utils/string/string_utils.c',
		'src/utils/render/render_helpers.c',
		'src/utils/wayland/wayland_manager.c',
	),
	include_directories: include_directories('src'),
	dependencies: [
		appindicator,
		cairo,
		client_protos,
		fontconfig,
		gdkpixbuf,
		libcurl,
		openssl,
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
