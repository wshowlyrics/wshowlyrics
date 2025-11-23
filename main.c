#include "main.h"

static void cairo_set_source_u32(cairo_t *cairo, const uint32_t color) {
	cairo_set_source_rgba(cairo,
			(color >> (3*8) & 0xFF) / 255.0,
			(color >> (2*8) & 0xFF) / 255.0,
			(color >> (1*8) & 0xFF) / 255.0,
			(color >> (0*8) & 0xFF) / 255.0);
}

static cairo_subpixel_order_t to_cairo_subpixel_order(
		enum wl_output_subpixel subpixel) {
	switch (subpixel) {
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
		return CAIRO_SUBPIXEL_ORDER_RGB;
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
		return CAIRO_SUBPIXEL_ORDER_BGR;
	case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
		return CAIRO_SUBPIXEL_ORDER_VRGB;
	case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
		return CAIRO_SUBPIXEL_ORDER_VBGR;
	default:
		return CAIRO_SUBPIXEL_ORDER_DEFAULT;
	}
	return CAIRO_SUBPIXEL_ORDER_DEFAULT;
}

static void render_to_cairo(cairo_t *cairo, struct lyrics_state *state,
		int scale, uint32_t *width, uint32_t *height) {
	const char *text_to_display = " "; // Default to single space
	bool has_lyrics = (state->current_line && state->current_line->text);
	bool is_empty_line = false; // Track if current line is empty (instrumental break)
	bool is_karaoke = false; // Track if this is karaoke-style LRCX

	if (has_lyrics) {
		// Check if the text is empty or only whitespace
		const char *text = state->current_line->text;
		if (text[0] == '\0') {
			// Empty string - treat as idle/instrumental break
			is_empty_line = true;
			text_to_display = " "; // Display single space to keep surface visible
		} else {
			text_to_display = text;
			// Check if this line has word segments for karaoke
			is_karaoke = (state->current_line->segment_count > 0 && state->current_line->segments != NULL);
		}
	}

	// Use transparent background when no lyrics or during instrumental breaks
	uint32_t background_color = (has_lyrics && !is_empty_line) ? state->background : 0x00000000;

	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_u32(cairo, background_color);
	cairo_paint(cairo);

	if (is_karaoke) {
		// Karaoke-style rendering with word highlighting
		int x_offset = 0;
		struct word_segment *segment = state->current_line->segments;
		bool found_current = false;

		while (segment) {
			// Determine segment state: past, current, or future
			bool is_current = (segment == state->current_segment);
			bool is_past = !found_current && !is_current;

			if (is_current) {
				found_current = true;
			}

			// Set color based on state:
			// - Past (sung): foreground color (like normal LRC)
			// - Current (singing): highlight color (sky blue by default)
			// - Future (not yet): dimmed (50% alpha)
			if (is_past) {
				// Already sung - use normal foreground
				cairo_set_source_u32(cairo, state->foreground);
			} else if (is_current) {
				// Currently singing - use highlight color
				cairo_set_source_u32(cairo, state->highlight);
			} else {
				// Not yet sung - dimmed
				uint32_t dimmed = state->foreground;
				uint8_t alpha = (dimmed & 0xFF);
				dimmed = (dimmed & 0xFFFFFF00) | (alpha / 2);
				cairo_set_source_u32(cairo, dimmed);
			}

			// Get size of this word segment
			int seg_w, seg_h;
			get_text_size(cairo, state->font, &seg_w, &seg_h, NULL, scale, "%s", segment->text);

			// Draw this word segment
			cairo_move_to(cairo, x_offset, 0);
			pango_printf(cairo, state->font, scale, "%s", segment->text);

			x_offset += seg_w;

			// Add space between words (except for last word)
			if (segment->next) {
				int space_w, space_h;
				get_text_size(cairo, state->font, &space_w, &space_h, NULL, scale, " ");
				x_offset += space_w;
			}

			segment = segment->next;
		}

		// Calculate total width and height
		int total_w, total_h;
		get_text_size(cairo, state->font, &total_w, &total_h, NULL, scale, "%s", text_to_display);
		*width = total_w;
		*height = total_h;
	} else {
		// Normal rendering (non-karaoke)
		cairo_set_source_u32(cairo, state->foreground);

		// Calculate text size
		int w, h;
		get_text_size(cairo, state->font, &w, &h, NULL, scale, "%s", text_to_display);

		// Draw text at the beginning of the surface (surface itself will be centered by layer-shell)
		cairo_move_to(cairo, 0, 0);
		pango_printf(cairo, state->font, scale, "%s", text_to_display);

		*width = w;
		*height = h;
	}
}

static void render_frame(struct lyrics_state *state) {
	cairo_surface_t *recorder = cairo_recording_surface_create(
			CAIRO_CONTENT_COLOR_ALPHA, NULL);
	cairo_t *cairo = cairo_create(recorder);
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
	if (state->output) {
		cairo_font_options_set_subpixel_order(
				fo, to_cairo_subpixel_order(state->output->subpixel));
	}
	cairo_set_font_options(cairo, fo);
	cairo_font_options_destroy(fo);
	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cairo);
	cairo_restore(cairo);

	const int scale = state->output ? state->output->scale : 1;
	uint32_t width = 0, height = 0;
	render_to_cairo(cairo, state, scale, &width, &height);

	if (height / scale != state->height
			|| width / scale != state->width
			|| state->width == 0) {
		// Size change detected - make overlay transparent during resize
		// First, render a transparent frame
		state->current_buffer = get_next_buffer(state->shm,
				state->buffers, state->width * scale, state->height * scale);
		if (state->current_buffer) {
			cairo_t *shm = state->current_buffer->cairo;
			cairo_save(shm);
			cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
			cairo_paint(shm);
			cairo_restore(shm);

			wl_surface_set_buffer_scale(state->surface, scale);
			wl_surface_attach(state->surface, state->current_buffer->buffer, 0, 0);
			wl_surface_damage_buffer(state->surface, 0, 0, state->width, state->height);
			wl_surface_commit(state->surface);
		}

		// Reconfigure surface size
		if (width == 0 || height == 0) {
			// Keep a minimal 1x1 surface instead of detaching
			zwlr_layer_surface_v1_set_size(state->layer_surface, 1, 1);
		} else {
			zwlr_layer_surface_v1_set_size(
					state->layer_surface, width / scale, height / scale);
		}

		wl_surface_commit(state->surface);
	} else if (height > 0) {
		// Replay recording into shm and send it off
		state->current_buffer = get_next_buffer(state->shm,
				state->buffers, state->width * scale, state->height * scale);
		if (!state->current_buffer) {
			cairo_surface_destroy(recorder);
			cairo_destroy(cairo);
			return;
		}
		cairo_t *shm = state->current_buffer->cairo;

		cairo_save(shm);
		cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
		cairo_paint(shm);
		cairo_restore(shm);

		cairo_set_source_surface(shm, recorder, 0.0, 0.0);
		cairo_paint(shm);

		wl_surface_set_buffer_scale(state->surface, scale);
		wl_surface_attach(state->surface,
				state->current_buffer->buffer, 0, 0);
		wl_surface_damage_buffer(state->surface, 0, 0,
				state->width, state->height);
		wl_surface_commit(state->surface);
	}

	cairo_surface_destroy(recorder);
	cairo_destroy(cairo);
}

static void set_dirty(struct lyrics_state *state) {
	if (state->frame_scheduled) {
		state->dirty = true;
	} else if (state->surface) {
		render_frame(state);
	}
}

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1,
		uint32_t serial, uint32_t width, uint32_t height) {
	struct lyrics_state *state = data;
	state->width = width;
	state->height = height;
	zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);
	set_dirty(state);
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1) {
	struct lyrics_state *state = data;
	state->run = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void surface_enter(void *data,
		struct wl_surface *wl_surface, struct wl_output *output) {
	struct lyrics_state *state = data;
	struct lyrics_output *lyrics_output = state->outputs;
	while (lyrics_output && lyrics_output->output != output) {
		lyrics_output = lyrics_output->next;
	}
	if (lyrics_output) {
		state->output = lyrics_output;
	}
}

static void surface_leave(void *data,
		struct wl_surface *wl_surface, struct wl_output *output) {
	// Not needed for this application
}

static const struct wl_surface_listener wl_surface_listener = {
	.enter = surface_enter,
	.leave = surface_leave,
};

static void output_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
		int32_t subpixel, const char *make, const char *model,
		int32_t transform) {
	struct lyrics_output *output = data;
	output->subpixel = subpixel;
}

static void output_mode(void *data, struct wl_output *wl_output,
		uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	struct lyrics_output *output = data;
	output->width = width;
	output->height = height;
	printf("Screen resolution: %dx%d\n", width, height);
}

static void output_done(void *data, struct wl_output *wl_output) {
	// Not needed
}

static void output_scale(void *data,
		struct wl_output *wl_output, int32_t factor) {
	struct lyrics_output *output = data;
	output->scale = factor;
}

static const struct wl_output_listener wl_output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
};

static void registry_global(void *data, struct wl_registry *wl_registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct lyrics_state *state = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(wl_registry,
				name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(wl_registry,
				name, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct lyrics_output *output = calloc(1, sizeof(struct lyrics_output));
		output->output = wl_registry_bind(wl_registry,
				name, &wl_output_interface, 3);
		output->scale = 1;
		output->height = 0;
		output->width = 0;
		struct lyrics_output **link = &state->outputs;
		while (*link) {
			link = &(*link)->next;
		}
		*link = output;
		wl_output_add_listener(output->output, &wl_output_listener, output);

		// Set first output as default
		if (!state->output) {
			state->output = output;
			printf("Set primary output\n");
		}
	}
}

static void registry_global_remove(void *data,
		struct wl_registry *wl_registry, uint32_t name) {
	/* This space deliberately left blank */
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static uint32_t parse_color(const char *color) {
	if (color[0] == '#') {
		++color;
	}

	const int len = strlen(color);
	if (len != 6 && len != 8) {
		fprintf(stderr, "Invalid color %s, defaulting to color "
				"0xFFFFFFFF\n", color);
		return 0xFFFFFFFF;
	}
	uint32_t res = (uint32_t)strtoul(color, NULL, 16);
	if (len == 6) {
		res = (res << 8) | 0xFF;
	}
	return res;
}

static bool update_track_info(struct lyrics_state *state) {

	struct track_metadata new_track = {0};
	if (!mpris_get_metadata(&new_track)) {
		return false;
	}

	// Check if track changed
	bool changed = false;
	if (!state->current_track.title ||
	    strcmp(new_track.title, state->current_track.title) != 0) {
		changed = true;
	}

	if (changed) {
		printf("\n=== Track changed ===\n");
		printf("Title: %s\n", new_track.title);
		printf("Artist: %s\n", new_track.artist ? new_track.artist : "Unknown");
		printf("Album: %s\n", new_track.album ? new_track.album : "Unknown");

		mpris_free_metadata(&state->current_track);
		state->current_track = new_track;
		state->track_changed = true;

		// Record when the track started
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		state->track_start_time_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
		state->track_start_time_us -= state->current_track.position_us;
	} else {
		mpris_free_metadata(&new_track);
	}

	return changed;
}

static bool load_lyrics_for_track(struct lyrics_state *state) {
	// Free previous lyrics
	lrc_free_data(&state->lyrics);
	state->current_line = NULL;

	// Try to find lyrics
	if (!lyrics_find_for_track(&state->current_track, &state->lyrics)) {
		printf("No lyrics found for current track\n");
		return false;
	}

	printf("Loaded %d lines of lyrics\n", state->lyrics.line_count);

	// Set initial line
	state->current_line = state->lyrics.lines;
	state->track_changed = false;

	return true;
}

static void update_current_line(struct lyrics_state *state) {
	if (!state->lyrics.lines) {
		return;
	}

	// Get current playback position
	int64_t position_us = mpris_get_position();

	// Find the appropriate line for current position
	struct lyrics_line *new_line = lrc_find_line_at_time(&state->lyrics, position_us);

	// Check if we should clear the lyrics
	if (new_line) {
		if (new_line->end_timestamp_us > 0) {
			// SRT/WEBVTT format: clear if we've passed the explicit end timestamp
			if (position_us > new_line->end_timestamp_us) {
				new_line = NULL;
			}
		}
		// LRC format: Keep displaying until next line starts (no automatic clearing)
		// This prevents lyrics from disappearing too quickly during instrumental breaks
	}

	bool line_changed = (new_line != state->current_line);
	if (line_changed) {
		state->current_line = new_line;
		state->current_segment = NULL; // Reset word segment when line changes

		if (new_line && new_line->text) {
			int index = lrc_get_line_index(&state->lyrics, new_line);
			printf("Line %d/%d: %s\n", index + 1, state->lyrics.line_count, new_line->text);

			// For karaoke (LRCX), set initial segment
			if (new_line->segments) {
				state->current_segment = new_line->segments;
			}
		} else if (!new_line) {
			printf("Instrumental break - clearing lyrics\n");
		}

		set_dirty(state);
	}

	// Update word segment for karaoke highlighting (LRCX)
	if (new_line && new_line->segments) {
		struct word_segment *new_segment = lrcx_find_segment_at_time(new_line, position_us, NULL);
		if (new_segment != state->current_segment) {
			state->current_segment = new_segment;
			set_dirty(state);
		}
	}
}

int main(int argc, char *argv[]) {
	int ret = 0;

	// Extract program name for help messages
	char *argv0_copy = strdup(argv[0]);
	const char *program_name = basename(argv0_copy);

	unsigned int anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	int margin = 32;
	struct lyrics_state state = { 0 };
	state.background = 0x00000080;
	state.foreground = 0xFFFFFFFF;
	state.highlight = 0x87CEEBFF; // Sky blue (default karaoke highlight color)
	state.font = "Sans 20";

	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"background", required_argument, 0, 'b'},
		{"foreground", required_argument, 0, 'f'},
		{"highlight", required_argument, 0, 'H'},
		{"font", required_argument, 0, 'F'},
		{"anchor", required_argument, 0, 'a'},
		{"margin", required_argument, 0, 'm'},
		{0, 0, 0, 0}
	};

	int c;
	int option_index = 0;
	while ((c = getopt_long(argc, argv, "hb:f:H:F:a:m:", long_options, &option_index)) != -1) {
		switch (c) {
		case 'b':
			state.background = parse_color(optarg);
			break;
		case 'f':
			state.foreground = parse_color(optarg);
			break;
		case 'H':
			state.highlight = parse_color(optarg);
			break;
		case 'F':
			state.font = optarg;
			break;
		case 'a':
			anchor = 0;
			if (strcmp(optarg, "top") == 0) {
				anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
			} else if (strcmp(optarg, "left") == 0) {
				anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
			} else if (strcmp(optarg, "right") == 0) {
				anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
			} else if (strcmp(optarg, "bottom") == 0) {
				anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
			}
			break;
		case 'm':
			margin = atoi(optarg);
			break;
		case 'h':
			// Normal help requested - use stdout
			fprintf(stdout, "Usage: %s [OPTIONS]\n\n", program_name);
			fprintf(stdout, "Wayland lyrics overlay with MPRIS integration\n\n");
			fprintf(stdout, "Options:\n");
			fprintf(stdout, "  -h, --help                   Show this help message\n");
			fprintf(stdout, "  -b, --background=COLOR       Background color in #RRGGBB[AA] format (default: #00000080)\n");
			fprintf(stdout, "  -f, --foreground=COLOR       Foreground/text color in #RRGGBB[AA] format (default: #FFFFFFFF)\n");
			fprintf(stdout, "  -H, --highlight=COLOR        Karaoke highlight color in #RRGGBB[AA] format (default: #87CEEBFF)\n");
			fprintf(stdout, "                               Used for currently singing word in LRCX format\n");
			fprintf(stdout, "  -F, --font=FONT              Font specification (default: \"Sans 20\")\n");
			fprintf(stdout, "                               Examples: \"Sans Bold 24\", \"Noto Sans CJK KR 18\"\n");
			fprintf(stdout, "  -a, --anchor=POSITION        Anchor position: top, bottom, left, right (default: bottom)\n");
			fprintf(stdout, "  -m, --margin=PIXELS          Margin from screen edge in pixels (default: 32)\n\n");
			fprintf(stdout, "Lyrics Detection:\n");
			fprintf(stdout, "  Automatically detects currently playing track via MPRIS and searches for lyrics in:\n");
			fprintf(stdout, "    1. Same directory as the music file\n");
			fprintf(stdout, "    2. Current directory\n");
			fprintf(stdout, "    3. ~/.lyrics/\n");
			fprintf(stdout, "    4. $HOME\n");
			fprintf(stdout, "    5. Online from lrclib.net API (if local files not found)\n\n");
			fprintf(stdout, "Supported Formats:\n");
			fprintf(stdout, "  - .lrcx: Karaoke-style with word-level timing\n");
			fprintf(stdout, "  - .lrc:  Standard LRC format with line-level timing\n");
			fprintf(stdout, "  - .srt:  SubRip subtitle format\n\n");
			fprintf(stdout, "Online Lyrics API:\n");
			fprintf(stdout, "  Automatically fetches synchronized lyrics from https://lrclib.net\n");
			fprintf(stdout, "  - Requires track title and artist metadata\n");
			fprintf(stdout, "  - Only uses synchronized lyrics (LRC format with timestamps)\n");
			fprintf(stdout, "  - Falls back gracefully if no internet connection\n");
			fprintf(stdout, "  - Privacy: Only sends song metadata (title, artist, album) to API\n\n");
			fprintf(stdout, "Examples:\n");
			fprintf(stdout, "  %s                                    # Auto-detect with MPRIS\n", program_name);
			fprintf(stdout, "  %s -F \"Sans Bold 24\"                  # Larger font\n", program_name);
			fprintf(stdout, "  %s --font=\"Sans Bold 24\"              # Same as above (long option)\n", program_name);
			fprintf(stdout, "  %s -a top -m 50                       # Top of screen, 50px margin\n", program_name);
			fprintf(stdout, "  %s --anchor=top --margin=50           # Same as above (long options)\n", program_name);
			fprintf(stdout, "  %s -b 000000AA -f FFFF00FF            # Custom colors\n", program_name);
			fprintf(stdout, "  %s --background=000000AA              # Custom background (long option)\n", program_name);
			fprintf(stdout, "  %s -H FF1493FF                        # Pink karaoke highlight\n", program_name);
			fprintf(stdout, "  %s --highlight=00FF00FF               # Green karaoke highlight\n", program_name);
			free(argv0_copy);
			return 0;
		default:
			// Error case - show brief error message
			fprintf(stderr, "Error: Invalid option\n");
			fprintf(stderr, "Usage: %s [OPTIONS]\n", program_name);
			fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
			free(argv0_copy);
			return 1;
		}
	}

	// Fontconfig initializations
	if(!FcInit()) {
		fprintf(stderr, "Failed to initialize fontconfig\n");
		return 1;
	}

	printf("Compositor: %s\n", getenv("WAYLAND_DISPLAY") ?: "wayland-0");
	printf("Using compositor interfaces...\n");

	// Initialize lyrics providers
	lyrics_providers_init();

	// Initialize MPRIS for automatic lyrics detection
	if (!mpris_init()) {
		fprintf(stderr, "Failed to initialize MPRIS (playerctl not found?)\n");
		ret = 1;
		goto exit;
	}
	printf("MPRIS mode enabled - will track currently playing music\n");

	state.display = wl_display_connect(NULL);
	if (!state.display) {
		fprintf(stderr, "wl_display_connect: %s\n", strerror(errno));
		ret = 1;
		goto exit;
	}

	state.registry = wl_display_get_registry(state.display);
	assert(state.registry);
	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	const struct {
		const char *name;
		void *ptr;
	} need_globals[] = {
		{"wl_compositor", &state.compositor},
		{"wl_shm", &state.shm},
		{"wlr_layer_shell", &state.layer_shell},
	};
	for (size_t i = 0; i < sizeof(need_globals) / sizeof(need_globals[0]); ++i) {
		if (!need_globals[i].ptr) {
			fprintf(stderr, "Error: required Wayland interface '%s' "
					"is not present\n", need_globals[i].name);
			ret = 1;
			goto exit;
		}
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	assert(state.surface);

	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			state.layer_shell, state.surface, NULL,
			ZWLR_LAYER_SHELL_V1_LAYER_TOP, "lyrics");
	assert(state.layer_surface);

	wl_surface_add_listener(state.surface, &wl_surface_listener, &state);
	zwlr_layer_surface_v1_add_listener(
			state.layer_surface, &layer_surface_listener, &state);
	zwlr_layer_surface_v1_set_size(state.layer_surface, 1, 1);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface, anchor);
	zwlr_layer_surface_v1_set_margin(state.layer_surface,
			margin, margin, margin, margin);
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, 0);

	// Set empty input region to allow clicks to pass through
	struct wl_region *region = wl_compositor_create_region(state.compositor);
	wl_surface_set_input_region(state.surface, region);
	wl_region_destroy(region);

	wl_surface_commit(state.surface);

	// Wait for configure event
	int retry_count = 0;
	while ((state.width == 0 || state.height == 0) && retry_count < 10) {
		wl_display_roundtrip(state.display);
		retry_count++;
	}

	retry_count = 0;
	while ((state.width == 0 || state.height == 0) && retry_count < 10) {
		wl_display_dispatch(state.display);
		retry_count++;
	}

	if (state.width == 0 || state.height == 0) {
		fprintf(stderr, "Layer surface configuration failed\n");
		ret = 1;
		goto exit;
	}

	struct pollfd pollfds[] = {
		{ .fd = wl_display_get_fd(state.display), .events = POLLIN, },
	};

	state.run = true;
	int update_counter = 0;

	while (state.run) {
		errno = 0;
		do {
			if (wl_display_flush(state.display) == -1 && errno != EAGAIN) {
				fprintf(stderr, "wl_display_flush: %s\n", strerror(errno));
				break;
			}
		} while (errno == EAGAIN);

		int timeout = 100; // 100ms update interval

		if (poll(pollfds, sizeof(pollfds) / sizeof(pollfds[0]), timeout) < 0) {
			fprintf(stderr, "poll: %s\n", strerror(errno));
			break;
		}

		// Check for track changes every 2 seconds (20 * 100ms)
		if (update_counter++ % 20 == 0) {
			if (update_track_info(&state)) {
				// Track changed, load new lyrics
				load_lyrics_for_track(&state);
				set_dirty(&state);
			}
		}

		// Update current line based on playback position
		if (mpris_is_playing()) {
			update_current_line(&state);
		} else {
			// Clear lyrics when not playing (paused or stopped)
			if (state.current_line != NULL) {
				state.current_line = NULL;
				set_dirty(&state);
				printf("Playback stopped/paused - clearing lyrics\n");
			}
		}

		if ((pollfds[0].revents & POLLIN)
				&& wl_display_dispatch(state.display) == -1) {
			fprintf(stderr, "wl_display_dispatch: %s\n", strerror(errno));
			break;
		}
	}

exit:
	lrc_free_data(&state.lyrics);
	mpris_free_metadata(&state.current_track);
	mpris_cleanup();
	lyrics_providers_cleanup();

	if (state.display) {
		wl_display_disconnect(state.display);
	}
	FcFini();
	free(argv0_copy);
	return ret;
}
