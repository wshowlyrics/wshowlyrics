// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CURL_UTILS_H
#define CURL_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <curl/curl.h>

// The most a response body may be unless the buffer says otherwise: far more
// than any answer of the APIs asked here (a lyrics search is around 100 KB),
// and little enough that a server streaming without end cannot fill the
// memory or keep the hand-written JSON reader busy.
#define CURL_RESPONSE_MAX_BYTES (2 * 1024 * 1024)

// Memory buffer for CURL responses
struct curl_memory_buffer {
    char *data;       // NUL-terminated
    size_t size;      // not counting the NUL
    size_t max_size;  // a body growing past this aborts the transfer
};

// Initialize memory buffer (empty, limited to CURL_RESPONSE_MAX_BYTES; set
// max_size afterwards for another limit)
void curl_memory_buffer_init(struct curl_memory_buffer *buffer);

// Free memory buffer
void curl_memory_buffer_free(struct curl_memory_buffer *buffer);

// CURL write callback for memory buffer. A body that would exceed the buffer's
// max_size aborts the transfer - as CURLE_WRITE_ERROR from here, or as
// CURLE_FILESIZE_EXCEEDED when libcurl (8.4 and newer) notices first.
size_t curl_write_to_memory(void *contents, size_t size, size_t nmemb, void *userp);

// Set a handle up to GET url into response, the way every plain download here
// is made: TLS 1.2 or newer, our User-Agent, 10 s in all and 5 s to connect,
// redirects followed, no signals, the body limited to response->max_size, and
// the calling thread's cancel flag watched. Options a caller needs different
// are set after this. False if an option could not be set.
bool curl_setup_get(CURL *curl, const char *url, struct curl_memory_buffer *response);

// URL encode a string for use in query parameters
// Returns encoded string (must be freed with curl_free)
// Returns NULL on error or if str is NULL
char* curl_url_encode(CURL *curl, const char *str);

// Make a transfer on this handle abort as soon as the thread running it is
// asked to stop (see thread_cancel.h). A no-op for a thread with no flag bound.
bool curl_install_thread_cancel(CURL *curl);

#endif // CURL_UTILS_H
