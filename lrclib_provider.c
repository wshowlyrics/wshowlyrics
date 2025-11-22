#include "lrclib_provider.h"
#include "lrc_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Memory buffer for CURL response
struct memory_buffer {
	char *data;
	size_t size;
};

// CURL write callback
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct memory_buffer *mem = (struct memory_buffer *)userp;

	char *ptr = realloc(mem->data, mem->size + realsize + 1);
	if (!ptr) {
		fprintf(stderr, "Not enough memory for CURL response\n");
		return 0;
	}

	mem->data = ptr;
	memcpy(&(mem->data[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->data[mem->size] = 0;

	return realsize;
}

// URL encode a string for use in query parameters
static char* url_encode(CURL *curl, const char *str) {
	if (!str) return NULL;
	return curl_easy_escape(curl, str, 0);
}

// Simple JSON string extractor (looks for "key":"value")
// Also unescapes \n to actual newlines
static char* extract_json_string(const char *json, const char *key) {
	if (!json || !key) return NULL;

	// Build search pattern: "key":"
	char pattern[256];
	snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

	char *start = strstr(json, pattern);
	if (!start) return NULL;

	start += strlen(pattern);
	char *end = start;

	// Find the closing quote, handling escaped characters
	while (*end) {
		if (*end == '"' && (end == start || *(end - 1) != '\\')) {
			break;
		}
		end++;
	}

	if (*end != '"') return NULL;

	size_t len = end - start;
	char *escaped = malloc(len + 1);
	if (!escaped) return NULL;

	memcpy(escaped, start, len);
	escaped[len] = '\0';

	// Unescape \n to actual newlines
	char *result = malloc(len + 1);
	if (!result) {
		free(escaped);
		return NULL;
	}

	size_t i = 0, j = 0;
	while (i < len) {
		if (escaped[i] == '\\' && i + 1 < len) {
			if (escaped[i + 1] == 'n') {
				result[j++] = '\n';
				i += 2;
			} else if (escaped[i + 1] == 't') {
				result[j++] = '\t';
				i += 2;
			} else if (escaped[i + 1] == 'r') {
				result[j++] = '\r';
				i += 2;
			} else if (escaped[i + 1] == '"') {
				result[j++] = '"';
				i += 2;
			} else if (escaped[i + 1] == '\\') {
				result[j++] = '\\';
				i += 2;
			} else {
				result[j++] = escaped[i++];
			}
		} else {
			result[j++] = escaped[i++];
		}
	}
	result[j] = '\0';

	free(escaped);
	return result;
}

// Parse duration in seconds from JSON (integer field)
static int64_t extract_json_duration(const char *json) {
	if (!json) return 0;

	char *duration_field = strstr(json, "\"duration\":");
	if (!duration_field) return 0;

	duration_field += strlen("\"duration\":");

	// Skip whitespace
	while (*duration_field == ' ' || *duration_field == '\t' || *duration_field == '\n') {
		duration_field++;
	}

	int64_t duration_sec = atoll(duration_field);
	return duration_sec * 1000000; // Convert to microseconds
}

static bool lrclib_search(const char *title, const char *artist, const char *album,
                          const char *url, struct lyrics_data *data) {
	// Require title, artist, and album for lrclib API
	if (!title || !artist || !album) {
		return false;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		fprintf(stderr, "Failed to initialize CURL\n");
		return false;
	}

	// URL encode parameters
	char *title_encoded = url_encode(curl, title);
	char *artist_encoded = url_encode(curl, artist);
	char *album_encoded = url_encode(curl, album);

	if (!title_encoded || !artist_encoded || !album_encoded) {
		curl_free(title_encoded);
		curl_free(artist_encoded);
		curl_free(album_encoded);
		curl_easy_cleanup(curl);
		return false;
	}

	// Build request URL
	char request_url[2048];
	snprintf(request_url, sizeof(request_url),
	         "https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s",
	         title_encoded, artist_encoded, album_encoded);

	printf("lrclib API request: %s\n", request_url);

	curl_free(title_encoded);
	curl_free(artist_encoded);
	curl_free(album_encoded);

	// Setup CURL request
	struct memory_buffer response = {0};
	curl_easy_setopt(curl, CURLOPT_URL, request_url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "wshowlyrics/0.1.0");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	// Perform request
	CURLcode res = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		fprintf(stderr, "lrclib API request failed: %s\n", curl_easy_strerror(res));
		free(response.data);
		return false;
	}

	if (http_code != 200) {
		printf("lrclib API returned HTTP %ld (no lyrics found)\n", http_code);
		free(response.data);
		return false;
	}

	// Parse JSON response
	if (!response.data) {
		return false;
	}

	// Extract syncedLyrics or plainLyrics
	char *synced_lyrics = extract_json_string(response.data, "syncedLyrics");
	char *plain_lyrics = extract_json_string(response.data, "plainLyrics");

	bool success = false;

	// Only use synced lyrics (LRC format) - skip plainLyrics
	if (synced_lyrics && strlen(synced_lyrics) > 0) {
		printf("Found synced lyrics from lrclib\n");
		success = lrc_parse_string(synced_lyrics, data);
	}

	free(synced_lyrics);
	free(plain_lyrics);
	free(response.data);

	return success;
}

static bool lrclib_init(void) {
	// Initialize CURL globally
	CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (res != CURLE_OK) {
		fprintf(stderr, "Failed to initialize CURL: %s\n", curl_easy_strerror(res));
		return false;
	}
	return true;
}

static void lrclib_cleanup(void) {
	curl_global_cleanup();
}

struct lyrics_provider lrclib_provider = {
	.name = "lrclib",
	.search = lrclib_search,
	.init = lrclib_init,
	.cleanup = lrclib_cleanup,
};
