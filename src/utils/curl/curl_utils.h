#ifndef CURL_UTILS_H
#define CURL_UTILS_H

#include <stddef.h>
#include <curl/curl.h>

// Memory buffer for CURL responses
struct curl_memory_buffer {
    char *data;
    size_t size;
};

// Initialize memory buffer
void curl_memory_buffer_init(struct curl_memory_buffer *buffer);

// Free memory buffer
void curl_memory_buffer_free(struct curl_memory_buffer *buffer);

// CURL write callback for memory buffer
size_t curl_write_to_memory(void *contents, size_t size, size_t nmemb, void *userp);

// URL encode a string for use in query parameters
// Returns encoded string (must be freed with curl_free)
// Returns NULL on error or if str is NULL
char* curl_url_encode(CURL *curl, const char *str);

#endif // CURL_UTILS_H
