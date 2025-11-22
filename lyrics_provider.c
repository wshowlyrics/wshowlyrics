#include "lyrics_provider.h"
#include "lrclib_provider.h"
#include "lrc_parser.h"
#include "srt_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

// ============================================================================
// Local file provider
// ============================================================================

static char* sanitize_filename(const char *str) {
	if (!str) return NULL;

	char *result = strdup(str);
	if (!result) return NULL;

	// Replace invalid filename characters
	for (char *p = result; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
		    *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
			*p = '_';
		}
	}

	return result;
}

static char* remove_extension(const char *str) {
	if (!str) return NULL;

	char *result = strdup(str);
	if (!result) return NULL;

	// Find the last dot
	char *dot = strrchr(result, '.');
	if (dot && dot != result) {
		// Common audio/video extensions to remove
		const char *exts[] = {
			".mp3", ".flac", ".ogg", ".opus", ".m4a", ".aac", ".wav",
			".mp4", ".mkv", ".avi", ".webm", ".mov",
			NULL
		};

		for (int i = 0; exts[i]; i++) {
			if (strcasecmp(dot, exts[i]) == 0) {
				*dot = '\0';
				break;
			}
		}
	}

	return result;
}

static bool try_load_lyrics_file(const char *path, struct lyrics_data *data) {
	struct stat st;
	if (stat(path, &st) != 0) {
		return false;
	}

	// Try LRC first
	if (lrc_parse_file(path, data)) {
		printf("Loaded LRC file: %s\n", path);
		return true;
	}

	// Try SRT
	if (srt_parse_file(path, data)) {
		printf("Loaded SRT file: %s\n", path);
		return true;
	}

	return false;
}

// URL decode a string (handles %XX encoding)
static char* url_decode_string(const char *str) {
	if (!str) return NULL;

	size_t len = strlen(str);
	char *decoded = malloc(len + 1);
	if (!decoded) return NULL;

	size_t i = 0, j = 0;
	while (i < len) {
		if (str[i] == '%' && i + 2 < len) {
			// Decode %XX
			char hex[3] = { str[i+1], str[i+2], '\0' };
			char *endptr;
			long val = strtol(hex, &endptr, 16);
			if (*endptr == '\0') {
				decoded[j++] = (char)val;
				i += 3;
				continue;
			}
		}
		decoded[j++] = str[i++];
	}
	decoded[j] = '\0';

	return decoded;
}

static char* get_directory_from_url(const char *url) {
	if (!url) return NULL;

	// Skip non-file URLs (http://, https://, spotify:, etc.)
	// These should not be used for local file searching
	if (strstr(url, "://") != NULL && strncmp(url, "file://", 7) != 0) {
		return NULL;
	}

	// Handle file:// URLs
	const char *path = url;
	if (strncmp(url, "file://", 7) == 0) {
		path = url + 7;
	}

	// URL decode the path to handle Unicode and special characters
	char *decoded_path = url_decode_string(path);
	if (!decoded_path) return NULL;

	// Get directory part
	char *last_slash = strrchr(decoded_path, '/');
	if (last_slash) {
		*last_slash = '\0';
	} else {
		free(decoded_path);
		return NULL;
	}

	printf("Extracted directory from URL: %s\n", decoded_path);
	return decoded_path;
}

// Extract filename from URL (without extension)
static char* get_filename_from_url(const char *url) {
	if (!url) return NULL;

	// Skip non-file URLs (http://, https://, spotify:, etc.)
	// These should not be used for local file searching
	if (strstr(url, "://") != NULL && strncmp(url, "file://", 7) != 0) {
		return NULL;
	}

	const char *path = url;
	if (strncmp(url, "file://", 7) == 0) {
		path = url + 7;
	}

	// URL decode
	char *decoded = url_decode_string(path);
	if (!decoded) return NULL;

	// Get filename part
	char *last_slash = strrchr(decoded, '/');
	char *filename = last_slash ? (last_slash + 1) : decoded;

	// Remove extension
	char *result = remove_extension(filename);

	free(decoded);
	return result;
}

static bool local_search(const char *title, const char *artist, const char *album,
                         const char *url, struct lyrics_data *data) {
	if (!title) {
		return false;
	}

	// Remove extension from title first (in case it's a filename)
	char *title_no_ext = remove_extension(title);
	char *title_safe = sanitize_filename(title_no_ext ? title_no_ext : title);
	char *artist_safe = artist ? sanitize_filename(artist) : NULL;
	free(title_no_ext);

	// PRIORITY 1: Directory of the currently playing file
	char *current_dir = get_directory_from_url(url);

	// Also try using the actual filename from URL
	char *filename_from_url = get_filename_from_url(url);

	// Try various locations and naming schemes
	const char *home = getenv("HOME");
	const char *xdg_music = getenv("XDG_MUSIC_DIR");

	const char *search_dirs[10] = {0};
	int dir_count = 0;

	// Add current directory as first priority
	if (current_dir) {
		search_dirs[dir_count++] = current_dir;
	}

	// Then try standard locations
	search_dirs[dir_count++] = ".";
	if (xdg_music) search_dirs[dir_count++] = xdg_music;

	// Add ~/.lyrics directory
	char lyrics_dir[256] = {0};
	if (home) {
		snprintf(lyrics_dir, sizeof(lyrics_dir), "%s/.lyrics", home);
		search_dirs[dir_count++] = lyrics_dir;
	}

	if (home) search_dirs[dir_count++] = home;

	char path[1024];

	for (int i = 0; i < dir_count; i++) {
		if (!search_dirs[i]) continue;

		// HIGHEST PRIORITY: Try exact filename from URL (in current directory)
		if (i == 0 && filename_from_url) {
			snprintf(path, sizeof(path), "%s/%s.lrc", search_dirs[i], filename_from_url);
			printf("Trying: %s\n", path);
			if (try_load_lyrics_file(path, data)) {
				goto success;
			}

			snprintf(path, sizeof(path), "%s/%s.srt", search_dirs[i], filename_from_url);
			printf("Trying: %s\n", path);
			if (try_load_lyrics_file(path, data)) {
				goto success;
			}
		}

		// Try: Title.lrc
		snprintf(path, sizeof(path), "%s/%s.lrc", search_dirs[i], title_safe);
		printf("Trying: %s\n", path);
		if (try_load_lyrics_file(path, data)) {
			goto success;
		}

		// Try: Title.srt
		snprintf(path, sizeof(path), "%s/%s.srt", search_dirs[i], title_safe);
		printf("Trying: %s\n", path);
		if (try_load_lyrics_file(path, data)) {
			goto success;
		}

		if (artist_safe) {
			// Try: Artist - Title.lrc
			snprintf(path, sizeof(path), "%s/%s - %s.lrc",
			         search_dirs[i], artist_safe, title_safe);
			if (try_load_lyrics_file(path, data)) {
				goto success;
			}

			// Try: Artist - Title.srt
			snprintf(path, sizeof(path), "%s/%s - %s.srt",
			         search_dirs[i], artist_safe, title_safe);
			if (try_load_lyrics_file(path, data)) {
				goto success;
			}

			// Try: Artist/Title.lrc
			snprintf(path, sizeof(path), "%s/%s/%s.lrc",
			         search_dirs[i], artist_safe, title_safe);
			if (try_load_lyrics_file(path, data)) {
				goto success;
			}
		}
	}

	free(title_safe);
	free(artist_safe);
	free(current_dir);
	free(filename_from_url);
	return false;

success:
	free(title_safe);
	free(artist_safe);
	free(current_dir);
	free(filename_from_url);
	return true;
}

static bool local_init(void) {
	return true; // Nothing to initialize
}

static void local_cleanup(void) {
	// Nothing to cleanup
}

struct lyrics_provider local_provider = {
	.name = "local",
	.search = local_search,
	.init = local_init,
	.cleanup = local_cleanup,
};

// ============================================================================
// High-level API
// ============================================================================

static struct lyrics_provider *providers[] = {
	&local_provider,
	&lrclib_provider,  // Try online if local not found
	NULL
};

void lyrics_providers_init(void) {
	for (int i = 0; providers[i]; i++) {
		if (providers[i]->init) {
			providers[i]->init();
		}
	}
}

void lyrics_providers_cleanup(void) {
	for (int i = 0; providers[i]; i++) {
		if (providers[i]->cleanup) {
			providers[i]->cleanup();
		}
	}
}

bool lyrics_find_for_track(struct track_metadata *track, struct lyrics_data *data) {
	if (!track || !track->title) {
		return false;
	}

	printf("Searching lyrics for: %s - %s\n",
	       track->artist ? track->artist : "Unknown", track->title);

	if (track->url) {
		printf("File location: %s\n", track->url);
	}

	// Try each provider in order
	for (int i = 0; providers[i]; i++) {
		printf("Trying provider: %s\n", providers[i]->name);
		if (providers[i]->search(track->title, track->artist, track->album,
		                         track->url, data)) {
			printf("Found lyrics via %s provider\n", providers[i]->name);
			return true;
		}
	}

	printf("No lyrics found\n");
	return false;
}
