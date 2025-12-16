#include "lang_detect.h"
#include "../../constants.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static bool detection_available = false;

#ifdef HAVE_LIBEXTTEXTCAT
#include <libexttextcat/textcat.h>

static void *textcat_handle = NULL;

/**
 * Convert BCP-47 language code to ISO 639-1
 * Examples: "[ko--utf8]" -> "ko", "[ja--utf8]" -> "ja", "[zh-CN-utf8]" -> "zh"
 *
 * Note: Uses alternating static buffers to allow two calls without overwriting results
 */
static char* normalize_language_code(const char *bcp47_code) {
	static char normalized[2][8];
	static int buffer_index = 0;

	// Alternate between two buffers
	buffer_index = (buffer_index + 1) % 2;
	char *result = normalized[buffer_index];

	if (!bcp47_code || strlen(bcp47_code) < 2) {
		return NULL;
	}

	// Skip opening bracket if present
	const char *start = bcp47_code;
	if (*start == '[') {
		start++;
	}

	// Extract language code (2-3 characters before hyphen or bracket)
	int i = 0;
	while (start[i] && start[i] != '-' && start[i] != ']' && i < 7) {
		result[i] = start[i];
		i++;
	}
	result[i] = '\0';

	return (i > 0) ? result : NULL;
}
#endif

bool lang_detect_init(void) {
#ifdef HAVE_LIBEXTTEXTCAT
	// Try to initialize libexttextcat with prefix
	const char *config_path = "/usr/share/libexttextcat/fpdb.conf";
	const char *prefix = "/usr/share/libexttextcat/";
	textcat_handle = special_textcat_Init(config_path, prefix);

	if (textcat_handle) {
		log_info("lang_detect: Using libexttextcat");
		detection_available = true;
		return true;
	} else {
		log_warn("lang_detect: libexttextcat initialization failed (config: %s, prefix: %s)",
		         config_path, prefix);
	}
#endif

	if (!detection_available) {
		log_info("lang_detect: No detection method available, language validation will be skipped");
	}

	return detection_available;
}

void lang_detect_cleanup(void) {
#ifdef HAVE_LIBEXTTEXTCAT
	if (textcat_handle) {
		textcat_Done(textcat_handle);
		textcat_handle = NULL;
	}
#endif

	detection_available = false;
}

#ifdef HAVE_LIBEXTTEXTCAT
static char* detect_via_exttextcat(const char *text, int max_len) {
	if (!textcat_handle || !text) {
		return NULL;
	}

	size_t text_len = strlen(text);
	size_t len = (max_len > 0) ? (size_t)max_len : text_len;
	if (len == 0) {
		return NULL;
	}

	// Classify the text
	const char *result = textcat_Classify(textcat_handle, text, len);

	if (!result || strcmp(result, "SHORT") == 0 || strcmp(result, "UNKNOWN") == 0) {
		// Detection failed or text too short
		return NULL;
	}

	// Convert BCP-47 to ISO 639-1 (e.g., "ko--utf8" -> "ko")
	return normalize_language_code(result);
}
#endif

/**
 * Remove common punctuation and special characters that might confuse language detection
 * Returns newly allocated string that must be freed by caller
 */
static char* strip_punctuation(const char *text) {
	if (!text) {
		return NULL;
	}

	size_t len = strlen(text);
	char *result = malloc(len + 1);
	if (!result) {
		return NULL;
	}

	size_t j = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)text[i];

		// Skip ASCII punctuation: !"#$%&'()*+,-./:;<=>?@[\]^_`{|}~
		if ((c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
		    (c >= '[' && c <= '`') || (c >= '{' && c <= '~')) {
			continue;
		}

		// Skip CJK punctuation (U+3000-U+303F)
		// UTF-8: E3 80 [80-BF] (3 bytes)
		if (i + 2 < len &&
		    (unsigned char)text[i] == 0xE3 &&
		    (unsigned char)text[i+1] == 0x80) {
			i += 2; // Skip all 3 bytes
			continue;
		}

		// Skip CJK punctuation (U+FF00-U+FFEF) - Fullwidth forms
		// UTF-8: EF BC [80-BF] or EF BD [80-AF] (3 bytes)
		if (i + 2 < len &&
		    (unsigned char)text[i] == 0xEF &&
		    ((unsigned char)text[i+1] == 0xBC || (unsigned char)text[i+1] == 0xBD)) {
			i += 2; // Skip all 3 bytes
			continue;
		}

		// Keep everything else (alphanumeric, spaces, and CJK characters)
		result[j++] = text[i];
	}
	result[j] = '\0';

	// If result is empty or too short, return NULL
	if (j < 3) {
		free(result);
		return NULL;
	}

	return result;
}

char* detect_language(const char *text, int max_len) {
	if (!text || strlen(text) == 0) {
		return NULL;
	}

	if (!detection_available) {
		return NULL;
	}

#ifdef HAVE_LIBEXTTEXTCAT
	// Strip punctuation to improve detection accuracy
	char *stripped = strip_punctuation(text);
	if (!stripped) {
		return NULL;
	}

	// Use libexttextcat for language detection
	char *result = detect_via_exttextcat(stripped, max_len);
	free(stripped);
	return result;
#else
	return NULL;
#endif
}

bool is_same_language(const char *text1, const char *text2) {
	if (!text1 || !text2) {
		return false;
	}

	if (!detection_available) {
		// No detection method available, skip validation
		log_warn("lang_detect: Detection not available, skipping validation");
		return false;
	}

	// Detect languages
	char *lang1 = detect_language(text1, -1);
	char *lang2 = detect_language(text2, -1);

	// If detection failed for either text, assume different languages (skip validation)
	// This is normal for short texts, numbers, or special characters
	if (!lang1 || !lang2) {
		return false;
	}

	// Compare language codes (case-insensitive)
	bool same = (strcasecmp(lang1, lang2) == 0);

	return same;
}
