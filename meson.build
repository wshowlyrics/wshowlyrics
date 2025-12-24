project(
	'lyrics',
	'c',
	version: '0.8.8',
	license: 'GPL',
	meson_version: '>=0.51.0',
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
jsonc          = dependency('json-c')
gio            = dependency('gio-2.0')

rt = cc.find_library('rt')

# Optional language detection libraries
exttextcat_dep = dependency('libexttextcat', required: false)

# Configure config.h with optional features
conf_data = configuration_data()
conf_data.set('HAVE_LIBEXTTEXTCAT', exttextcat_dep.found())

configure_file(
	output: 'config.h',
	configuration: conf_data
)

subdir('protocols')

executable(
	'lyrics',
	files(
		'src/main.c',
		'src/core/state/state_helpers.c',
		'src/core/rendering/rendering_manager.c',
		'src/lyrics/lyrics_manager.c',
		'src/monitor/file_monitor.c',
		'src/events/wayland_events.c',
		'src/utils/pango/pango_utils.c',
		'src/utils/shm/shm.c',
		'src/utils/mpris/mpris.c',
		'src/utils/dbus_control/dbus_control.c',
		'src/parser/lrc/lrc_common.c',
		'src/parser/lrc/lrc_parser.c',
		'src/parser/lrc/lrcx_parser.c',
		'src/parser/srt/srt_parser.c',
		'src/provider/lyrics/lyrics_provider.c',
		'src/provider/lrclib/lrclib_provider.c',
		'src/provider/itunes/itunes_artwork.c',
		'src/translator/common/translator_common.c',
		'src/translator/deepl/deepl_translator.c',
		'src/translator/gemini/gemini_translator.c',
		'src/translator/claude/claude_translator.c',
		'src/translator/openai/openai_translator.c',
		'src/user_experience/system_tray/system_tray.c',
		'src/user_experience/config/config.c',
		'src/parser/utils/parser_utils.c',
		'src/utils/curl/curl_utils.c',
		'src/utils/file/file_utils.c',
		'src/utils/json/json_utils.c',
		'src/utils/string/string_utils.c',
		'src/utils/lang_detect/lang_detect.c',
		'src/utils/render/render_common.c',
		'src/utils/render/ruby_render.c',
		'src/utils/render/word_render.c',
		'src/utils/wayland/wayland_manager.c',
		'src/utils/wayland/wayland_init.c',
	),
	include_directories: include_directories('src'),
	dependencies: [
		appindicator,
		cairo,
		client_protos,
		fontconfig,
		gdkpixbuf,
		gio,
		jsonc,
		libcurl,
		openssl,
		pango,
		pangocairo,
		rt,
		wayland_client,
		wayland_protos,
		exttextcat_dep,
	],
	install: true,
)

# Install system-wide configuration file
install_data(
	'settings.ini.example',
	install_dir: '/etc/wshowlyrics',
	rename: 'settings.ini',
)

# Install systemd user service file
install_data(
	'wshowlyrics.service',
	install_dir: '/usr/lib/systemd/user',
)

# Install helper script for runtime control
install_data(
	'wshowlyrics-offset',
	install_dir: get_option('bindir'),
	install_mode: 'rwxr-xr-x',
)
