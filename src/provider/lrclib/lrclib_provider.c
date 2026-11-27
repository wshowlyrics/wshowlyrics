// SPDX-License-Identifier: GPL-3.0-or-later
#include "lrclib_provider.h"
#include "lrclib_response.h"
#include "../../parser/lrc/lrc_parser.h"
#include "../../utils/curl/curl_utils.h"
#include "../../utils/string/string_utils.h"
#include "../../utils/json/json_utils.h"
#include "../../utils/thread/thread_cancel.h"
#include "../../constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Where the API lives. A test build can be pointed at a mock server on this
// machine instead (meson setup build-mock -Dlrclib_mock_port=8071);
// there is no way to change it at run time.
#ifdef LRCLIB_MOCK_PORT
#define LRCLIB_STRINGIFY_(x) #x
#define LRCLIB_STRINGIFY(x) LRCLIB_STRINGIFY_(x)
#define LRCLIB_BASE_URL "http://127.0.0.1:" LRCLIB_STRINGIFY(LRCLIB_MOCK_PORT)
#else
#define LRCLIB_BASE_URL "https://lrclib.net"
#endif

// A request that fails for a reason that may pass - the server answering 5xx or
// 429, a timeout, no connection - is repeated, because otherwise one hiccup
// leaves the track without lyrics until it ends (#22). Anything else is final:
// "not found" will still be not found a second later.
//
// The attempts are counted per lookup, not per request: the exact match and
// the search that may follow it share them, so a server that keeps timing out
// costs one lookup about half a minute at most, not a minute.
#define LRCLIB_MAX_FAILED_ATTEMPTS 3
// Wait after the first and after the second failed attempt
static const int RETRY_WAIT_MS[LRCLIB_MAX_FAILED_ATTEMPTS - 1] = { 1000, 3000 };
// A server that asks (Retry-After) for more patience than this is not waited
// for; the track simply goes without online lyrics this time
#define LRCLIB_MAX_RETRY_WAIT_MS 5000

// How a request, or a whole lookup, ended
enum lrclib_outcome {
    LRCLIB_OK,           // answered 200 / lyrics found
    LRCLIB_NOT_FOUND,    // the server knows no such track
    LRCLIB_REJECTED,     // the server refused this request (a 4xx other than
                         // 404/429): not worth repeating, but nothing is wrong
                         // with the server either
    LRCLIB_TRANSIENT,    // failed in a way worth trying again (never returned
                         // by lrclib_fetch or anything above it)
    LRCLIB_UNAVAILABLE,  // no answer to be had: attempts used up, cancelled, or
                         // a transfer error retrying will not cure
};

// One lookup: the handle its requests share, and how many attempts have failed
struct lrclib_session {
    CURL *curl;
    int failed_attempts;
};

static bool is_transient_curl_error(CURLcode res) {
    return res == CURLE_OPERATION_TIMEDOUT || res == CURLE_COULDNT_CONNECT ||
           res == CURLE_COULDNT_RESOLVE_HOST || res == CURLE_RECV_ERROR ||
           res == CURLE_SEND_ERROR || res == CURLE_GOT_NOTHING;
}

// Sort one finished transfer into an outcome
static enum lrclib_outcome classify_transfer(CURLcode res, long http_code) {
    if (res != CURLE_OK) {
        return is_transient_curl_error(res) ? LRCLIB_TRANSIENT : LRCLIB_UNAVAILABLE;
    }
    if (http_code == HTTP_OK) {
        return LRCLIB_OK;
    }
    if (http_code == HTTP_NOT_FOUND) {
        return LRCLIB_NOT_FOUND;
    }
    if (http_code == HTTP_TOO_MANY_REQUESTS || http_code >= HTTP_SERVER_ERROR_MIN) {
        return LRCLIB_TRANSIENT;
    }
    return LRCLIB_REJECTED;
}

// The options every request of a lookup shares; set once per session
static bool setup_session(CURL *curl) {
    // TLS 1.2 or higher
    return curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
           curl_install_thread_cancel(curl) &&
           curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK;
}

// How long to wait before the next attempt: the schedule above, or what the
// server asked for if that is longer. Negative if it asked for too long.
static int retry_wait_ms(const struct lrclib_session *session) {
    curl_off_t retry_after_s = 0;
    curl_easy_getinfo(session->curl, CURLINFO_RETRY_AFTER, &retry_after_s);
    if (retry_after_s > LRCLIB_MAX_RETRY_WAIT_MS / 1000) {
        return -1;
    }
    const int asked_ms = (int)retry_after_s * 1000;
    const int scheduled_ms = RETRY_WAIT_MS[session->failed_attempts - 1];
    return asked_ms > scheduled_ms ? asked_ms : scheduled_ms;
}

// After a transient failure: count it, report it and wait for the next
// attempt. False to stop - out of attempts, asked to wait too long, or
// cancelled while waiting.
static bool wait_for_retry(struct lrclib_session *session, const char *reason) {
    session->failed_attempts++;
    if (session->failed_attempts >= LRCLIB_MAX_FAILED_ATTEMPTS) {
        log_warn("lrclib request failed (%s); gave up after %d failed attempts",
                 reason, session->failed_attempts);
        return false;
    }
    const int wait_ms = retry_wait_ms(session);
    if (wait_ms < 0) {
        log_warn("lrclib request failed (%s); the server asks to wait more than %d s, not retrying",
                 reason, LRCLIB_MAX_RETRY_WAIT_MS / 1000);
        return false;
    }
    log_warn("lrclib request failed (%s); retrying in %d ms (failed attempt %d of %d)",
             reason, wait_ms, session->failed_attempts, LRCLIB_MAX_FAILED_ATTEMPTS);
    return thread_cancel_sleep_ms(wait_ms);
}

// One attempt. On LRCLIB_OK the body is in *response (the caller frees it);
// otherwise *response is empty. LRCLIB_TRANSIENT means "go again".
static enum lrclib_outcome attempt_request(struct lrclib_session *session, const char *url,
                                           struct curl_memory_buffer *response) {
    CURL *curl = session->curl;
    curl_memory_buffer_init(response);
    if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response) != CURLE_OK) {
        log_error("lrclib: Failed to set CURL options");
        return LRCLIB_UNAVAILABLE;
    }

    const CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    enum lrclib_outcome outcome = classify_transfer(res, http_code);
    if (outcome == LRCLIB_OK) {
        return outcome;
    }
    curl_memory_buffer_free(response);

    if (thread_cancel_requested()) {
        // The track changed; whatever the transfer says, nobody is waiting
        return LRCLIB_UNAVAILABLE;
    }
    if (outcome == LRCLIB_NOT_FOUND) {
        return outcome;
    }

    char reason[96];
    if (res == CURLE_OK) {
        snprintf(reason, sizeof(reason), "HTTP %ld", http_code);
    } else {
        snprintf(reason, sizeof(reason), "%s", curl_easy_strerror(res));
    }

    if (outcome == LRCLIB_TRANSIENT) {
        outcome = wait_for_retry(session, reason) ? LRCLIB_TRANSIENT : LRCLIB_UNAVAILABLE;
    } else {
        log_warn("lrclib request failed (%s)", reason);
    }
    return outcome;
}

// GET url, repeating transient failures while the session has attempts left
static enum lrclib_outcome lrclib_fetch(struct lrclib_session *session, const char *url,
                                        struct curl_memory_buffer *response) {
    enum lrclib_outcome outcome = LRCLIB_TRANSIENT;
    while (outcome == LRCLIB_TRANSIENT) {
        outcome = attempt_request(session, url, response);
    }
    return outcome;
}

// Build search request URL with sanitized title and optional artist
static bool build_search_request_url(CURL *curl, const char *title, const char *artist,
                                     char *url_buffer, size_t buffer_size) {
    // Sanitize title to remove YouTube IDs and file extensions
    char *clean_title = sanitize_title(title);
    if (!clean_title || clean_title[0] == '\0') {
        free(clean_title);
        return false;
    }

    log_info("Sanitized title: '%s' -> '%s'", title, clean_title);

    // Build search query - use track_name with sanitized title
    char *title_encoded = curl_url_encode(curl, clean_title);
    free(clean_title);

    if (!title_encoded) {
        return false;
    }

    int offset = snprintf(url_buffer, buffer_size,
                         LRCLIB_BASE_URL "/api/search?track_name=%s", title_encoded);
    curl_free(title_encoded);

    // Check for truncation - if URL already truncated, skip artist parameter
    if (offset < 0 || (size_t)offset >= buffer_size) {
        return true;  // URL truncated but still usable (just without artist)
    }

    // Add artist if available
    if (str_is_set(artist)) {
        char *artist_encoded = curl_url_encode(curl, artist);
        if (artist_encoded) {
            snprintf(url_buffer + offset, buffer_size - offset,
                    "&artist_name=%s", artist_encoded);
            curl_free(artist_encoded);
        }
    }

    return true;
}

// Build the exact match request URL (with the track's length if it is known)
static bool build_get_request_url(CURL *curl, const char *title, const char *artist,
                                  const char *album, int64_t duration_ms,
                                  char *url_buffer, size_t buffer_size) {
    char *title_encoded = curl_url_encode(curl, title);
    char *artist_encoded = curl_url_encode(curl, artist);
    char *album_encoded = curl_url_encode(curl, album);

    const bool encoded = title_encoded && artist_encoded && album_encoded;
    if (encoded && duration_ms > 0) {
        snprintf(url_buffer, buffer_size,
                 LRCLIB_BASE_URL "/api/get?track_name=%s&artist_name=%s&album_name=%s&duration=%ld",
                 title_encoded, artist_encoded, album_encoded, duration_ms / 1000);
    } else if (encoded) {
        snprintf(url_buffer, buffer_size,
                 LRCLIB_BASE_URL "/api/get?track_name=%s&artist_name=%s&album_name=%s",
                 title_encoded, artist_encoded, album_encoded);
    }

    curl_free(title_encoded);
    curl_free(artist_encoded);
    curl_free(album_encoded);
    return encoded;
}

// Extract and update metadata from matched result
static void extract_metadata_from_result(const char *obj_start, struct lyrics_data *data) {
    if (!obj_start) return;

    char *artist_name = json_extract_string(obj_start, "artistName");
    char *album_name = json_extract_string(obj_start, "albumName");
    char *track_name = json_extract_string(obj_start, "trackName");

    if (artist_name && artist_name[0] != '\0') {
        free(data->metadata.artist);
        data->metadata.artist = artist_name;
        log_info("lrclib metadata: artist = %s", artist_name);
    } else {
        free(artist_name);
    }

    if (album_name && album_name[0] != '\0') {
        free(data->metadata.album);
        data->metadata.album = album_name;
        log_info("lrclib metadata: album = %s", album_name);
    } else {
        free(album_name);
    }

    if (track_name && track_name[0] != '\0') {
        free(data->metadata.title);
        data->metadata.title = track_name;
        log_info("lrclib metadata: title = %s", track_name);
    } else {
        free(track_name);
    }
}

// The search API: by name (and artist, if there is one), then the result
// closest in length - if it is within LRCLIB_MAX_DURATION_DIFF_MS. Used when
// the artist or album is missing, or when the exact match found nothing. The
// album is left out on purpose: a wrong album tag would hide the right song.
// LRCLIB_NOT_FOUND covers every way of the server having nothing usable.
static enum lrclib_outcome lrclib_search_by_name(struct lrclib_session *session,
                                                 const char *title, const char *artist,
                                                 int64_t duration_ms, struct lyrics_data *data) {
    // A title alone, with no length to check the result against, would accept
    // the first song of that name
    if (duration_ms <= 0 && !str_is_set(artist)) {
        log_info("lrclib search skipped: no artist and no track length to tell songs apart");
        return LRCLIB_NOT_FOUND;
    }

    char request_url[URL_BUFFER_SIZE];
    if (!build_search_request_url(session->curl, title, artist, request_url, sizeof(request_url))) {
        return LRCLIB_UNAVAILABLE;
    }
    log_info("lrclib search API request: %s", request_url);

    struct curl_memory_buffer response;
    const enum lrclib_outcome outcome = lrclib_fetch(session, request_url, &response);
    if (outcome == LRCLIB_NOT_FOUND) {
        log_info("lrclib search returned HTTP 404 (no results)");
    }
    if (outcome != LRCLIB_OK) {
        return outcome;
    }

    // Validate response format
    if (!response.data || response.size < 2 || response.data[0] != '[') {
        log_warn("lrclib search answered with something that is not a result list");
        curl_memory_buffer_free(&response);
        return LRCLIB_UNAVAILABLE;
    }

    struct lrclib_match match = lrclib_find_best_match(response.data, duration_ms);

    bool success = false;
    if (match.synced_lyrics && duration_ms > 0 &&
        match.duration_diff > LRCLIB_MAX_DURATION_DIFF_MS) {
        log_info("Closest lrclib search result is %ld ms off the track's length "
                 "(limit %d ms): another song or another cut, not used",
                 match.duration_diff, LRCLIB_MAX_DURATION_DIFF_MS);
    } else if (match.synced_lyrics) {
        if (duration_ms > 0) {
            log_info("Selected best match with duration diff: %ld ms", match.duration_diff);
        }
        log_info("Found synced lyrics from lrclib search");
        success = lrc_parse_string(match.synced_lyrics, data);

        if (success) {
            extract_metadata_from_result(match.obj_start, data);
        }
    } else {
        log_info("No usable synced lyrics in search results");
    }

    free(match.synced_lyrics);
    curl_memory_buffer_free(&response);
    return success ? LRCLIB_OK : LRCLIB_NOT_FOUND;
}

// The exact match API: title, artist, album and (if known) length must all
// agree. LRCLIB_OK: lyrics are in *data. LRCLIB_NOT_FOUND: the server has no
// such track, or only unsynced lyrics for it. Otherwise as lrclib_fetch.
static enum lrclib_outcome lrclib_exact_match(struct lrclib_session *session, const char *title,
                                              const char *artist, const char *album,
                                              int64_t duration_ms, struct lyrics_data *data) {
    char request_url[URL_BUFFER_SIZE];
    if (!build_get_request_url(session->curl, title, artist, album, duration_ms,
                               request_url, sizeof(request_url))) {
        return LRCLIB_UNAVAILABLE;
    }
    log_info("lrclib API request: %s", request_url);

    struct curl_memory_buffer response;
    const enum lrclib_outcome outcome = lrclib_fetch(session, request_url, &response);
    if (outcome == LRCLIB_NOT_FOUND) {
        log_info("lrclib API returned HTTP 404 (no exact match found)");
    }
    if (outcome != LRCLIB_OK) {
        return outcome;
    }

    // Only synced lyrics (LRC format) are used; plainLyrics would lose the sync
    char *synced_lyrics = response.data ? json_extract_string(response.data, "syncedLyrics") : NULL;
    bool parsed = false;
    if (str_is_set(synced_lyrics)) {
        log_info("Found synced lyrics from lrclib exact match");
        parsed = lrc_parse_string(synced_lyrics, data);
    } else {
        log_info("No synced lyrics in exact match");
    }

    free(synced_lyrics);
    curl_memory_buffer_free(&response);
    return parsed ? LRCLIB_OK : LRCLIB_NOT_FOUND;
}

// Ask lrclib for the track's lyrics. The outcome says why there are none, so
// that "the server has none" (LRCLIB_NOT_FOUND) can later be remembered while
// "the server could not be asked" must not be (#10).
static enum lrclib_outcome lrclib_lookup(struct lrclib_session *session, const char *title,
                                         const char *artist, const char *album,
                                         int64_t duration_ms, struct lyrics_data *data) {
    // The exact match needs artist and album
    enum lrclib_outcome outcome = LRCLIB_NOT_FOUND;
    if (str_is_set(artist) && str_is_set(album)) {
        outcome = lrclib_exact_match(session, title, artist, album, duration_ms, data);
    } else {
        log_info("Missing metadata (artist: %s, album: %s), using search API",
               artist ? artist : "none", album ? album : "none");
    }

    // Without an exact match - none found, or that request refused - search by
    // name. Not when the server could not be reached: the search API is the
    // same server, and asking it would only add to the wait.
    if (outcome == LRCLIB_NOT_FOUND || outcome == LRCLIB_REJECTED) {
        outcome = lrclib_search_by_name(session, title, artist, duration_ms, data);
    }
    return outcome;
}

static bool lrclib_search(const char *title, const char *artist, const char *album,
                          const char *url, int64_t duration_ms, struct lyrics_data *data) {
    (void)url; // Unused for online search

    if (!str_is_set(title)) {
        log_info("Missing title, cannot search lrclib");
        return false;
    }

    struct lrclib_session session = { .curl = curl_easy_init() };
    if (!session.curl) {
        log_error("Failed to initialize CURL");
        return false;
    }

    bool found = false;
    if (setup_session(session.curl)) {
        found = lrclib_lookup(&session, title, artist, album, duration_ms, data) == LRCLIB_OK;
    } else {
        log_error("lrclib: Failed to set CURL options");
    }

    curl_easy_cleanup(session.curl);
    return found;
}

static bool lrclib_init(void) {
#ifdef LRCLIB_MOCK_PORT
    log_warn("Test build: lrclib requests go to " LRCLIB_BASE_URL ", not to lrclib.net");
#endif
    // Initialize CURL globally
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        log_error("Failed to initialize CURL: %s", curl_easy_strerror(res));
        return false;
    }
    return true;
}

static void lrclib_cleanup(void) {
    curl_global_cleanup();
}

static bool lrclib_is_enabled(const struct lyrics_search_options *options) {
    return options->enable_lrclib;
}

struct lyrics_provider lrclib_provider = {
    .name = "lrclib",
    .search = lrclib_search,
    .init = lrclib_init,
    .cleanup = lrclib_cleanup,
    .is_enabled = lrclib_is_enabled,
};
