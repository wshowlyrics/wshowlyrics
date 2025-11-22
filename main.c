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

	if (has_lyrics) {
		// Check if the text is empty or only whitespace
		const char *text = state->current_line->text;
		if (text[0] == '\0') {
			// Empty string - treat as idle/instrumental break
			is_empty_line = true;
			text_to_display = " "; // Display single space to keep surface visible
		} else {
			text_to_display = text;
		}
	}

	// Use transparent background when no lyrics or during instrumental breaks (empty lines)
	uint32_t background_color = (has_lyrics && !is_empty_line) ? state->background : 0x00000000;

	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_u32(cairo, background_color);
	cairo_paint(cairo);

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
		// Reconfigure surface
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
	fprintf(stdout, "Screen resolution: %dx%d\n", width, height);
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
	if (strlen(color) == 6) {
		res = (res << 8) | 0xFF;
	}
	return res;
}

static bool update_track_info(struct lyrics_state *state) {
	if (!state->use_mpris) {
		return false;
	}

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
	if (!state->lyrics.lines || !state->use_mpris) {
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

	if (new_line != state->current_line) {
		state->current_line = new_line;
		set_dirty(state);

		if (new_line && new_line->text) {
			int index = lrc_get_line_index(&state->lyrics, new_line);
			printf("Line %d/%d: %s\n", index + 1, state->lyrics.line_count, new_line->text);
		} else if (!new_line) {
			printf("Instrumental break - clearing lyrics\n");
		}
	}
}

int main(int argc, char *argv[]) {
	// Fontconfig initializations
	if(!FcInit()) {
		fprintf(stderr, "Failed to initialize fontconfig\n");
		return 1;
	}

	fprintf(stdout, "Compositor: %s\n", getenv("WAYLAND_DISPLAY") ?: "wayland-0");
	fprintf(stdout, "Using compositor interfaces...\n");

	int ret = 0;

	unsigned int anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	int margin = 32;
	struct lyrics_state state = { 0 };
	state.background = 0x00000080;
	state.foreground = 0xFFFFFFFF;
	state.font = "Sans 20";
	state.lyrics_file = NULL;
	state.use_mpris = true; // Default to MPRIS mode

	int c;
	while ((c = getopt(argc, argv, "hb:f:F:a:m:l:")) != -1) {
		switch (c) {
		case 'b':
			state.background = parse_color(optarg);
			break;
		case 'f':
			state.foreground = parse_color(optarg);
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
		case 'l':
			state.lyrics_file = optarg;
			state.use_mpris = false; // Explicit file disables MPRIS
			break;
		case 'h':
		default:
			fprintf(stderr, "Usage: lyrics [OPTIONS]\n\n");
			fprintf(stderr, "Wayland lyrics overlay with MPRIS integration\n\n");
			fprintf(stderr, "Options:\n");
			fprintf(stderr, "  -h              Show this help message\n");
			fprintf(stderr, "  -b COLOR        Background color in #RRGGBB[AA] format (default: #00000080)\n");
			fprintf(stderr, "  -f COLOR        Foreground/text color in #RRGGBB[AA] format (default: #FFFFFFFF)\n");
			fprintf(stderr, "  -F FONT         Font specification (default: \"Sans 20\")\n");
			fprintf(stderr, "                  Examples: \"Sans Bold 24\", \"Noto Sans CJK KR 18\"\n");
			fprintf(stderr, "  -a POSITION     Anchor position: top, bottom, left, right (default: bottom)\n");
			fprintf(stderr, "  -m PIXELS       Margin from screen edge in pixels (default: 32)\n");
			fprintf(stderr, "  -l FILE         Load specific lyrics file (disables MPRIS auto-detection)\n");
			fprintf(stderr, "                  Supports: .lrc, .srt, .vtt formats\n\n");
			fprintf(stderr, "MPRIS Mode (default):\n");
			fprintf(stderr, "  When -l is not provided, automatically detects currently playing track\n");
			fprintf(stderr, "  and searches for lyrics files in:\n");
			fprintf(stderr, "    1. Same directory as the music file\n");
			fprintf(stderr, "    2. Current directory\n");
			fprintf(stderr, "    3. ~/.lyrics/\n");
			fprintf(stderr, "    4. $HOME\n\n");
			fprintf(stderr, "Examples:\n");
			fprintf(stderr, "  lyrics                              # Auto-detect with MPRIS\n");
			fprintf(stderr, "  lyrics -F \"Sans Bold 24\"            # Larger font\n");
			fprintf(stderr, "  lyrics -a top -m 50                 # Top of screen, 50px margin\n");
			fprintf(stderr, "  lyrics -b 000000AA -f FFFF00FF      # Custom colors\n");
			fprintf(stderr, "  lyrics -l song.lrc                  # Load specific file\n");
			return c == 'h' ? 0 : 1;
		}
	}

	// Initialize lyrics providers
	lyrics_providers_init();

	// If using MPRIS mode
	if (state.use_mpris) {
		if (!mpris_init()) {
			fprintf(stderr, "Failed to initialize MPRIS (playerctl not found?)\n");
			ret = 1;
			goto exit;
		}
		printf("MPRIS mode enabled - will track currently playing music\n");
	} else if (state.lyrics_file) {
		// Load static lyrics file
		printf("Loading static lyrics file: %s\n", state.lyrics_file);
		if (!lrc_parse_file(state.lyrics_file, &state.lyrics) &&
		    !srt_parse_file(state.lyrics_file, &state.lyrics)) {
			fprintf(stderr, "Failed to load lyrics file\n");
			ret = 1;
			goto exit;
		}

		// In manual mode, still use MPRIS for timing if available
		if (mpris_init()) {
			printf("MPRIS available - will sync with playback position\n");
			state.use_mpris = true;
		} else {
			printf("MPRIS not available - displaying first line only\n");
		}

		state.current_line = state.lyrics.lines;
	} else {
		fprintf(stderr, "Error: Either use MPRIS mode (default) or provide -l lyrics_file\n");
		ret = 1;
		goto exit;
	}

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

		int timeout = state.use_mpris ? 100 : 1000; // 100ms for MPRIS, 1s for static

		if (poll(pollfds, sizeof(pollfds) / sizeof(pollfds[0]), timeout) < 0) {
			fprintf(stderr, "poll: %s\n", strerror(errno));
			break;
		}

		// In MPRIS mode, periodically check for track changes and update position
		if (state.use_mpris) {
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
	return ret;
}
