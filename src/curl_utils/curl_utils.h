#ifndef CURL_UTILS_H
#define CURL_UTILS_H

#include <stddef.h>

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

#endif // CURL_UTILS_H
