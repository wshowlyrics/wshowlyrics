#include "curl_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void curl_memory_buffer_init(struct curl_memory_buffer *buffer) {
    buffer->data = NULL;
    buffer->size = 0;
}

void curl_memory_buffer_free(struct curl_memory_buffer *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}

size_t curl_write_to_memory(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct curl_memory_buffer *mem = (struct curl_memory_buffer *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Out of memory for CURL response\n");
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}
