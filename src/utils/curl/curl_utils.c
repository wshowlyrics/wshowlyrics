// SPDX-License-Identifier: GPL-3.0-or-later
#include "curl_utils.h"
#include "../../constants.h"
#include "../thread/thread_cancel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// curl runs this on the thread that performs the transfer, so it sees that
// thread's flag. Non-zero aborts the transfer (CURLE_ABORTED_BY_CALLBACK).
static int thread_cancel_xferinfo_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                     curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return thread_cancel_requested() ? 1 : 0;
}

bool curl_install_thread_cancel(CURL *curl) {
    return curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, thread_cancel_xferinfo_cb) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) == CURLE_OK;
}

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
        log_error("Out of memory for CURL response");
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

char* curl_url_encode(CURL *curl, const char *str) {
    if (!str) return NULL;
    return curl_easy_escape(curl, str, 0);
}
