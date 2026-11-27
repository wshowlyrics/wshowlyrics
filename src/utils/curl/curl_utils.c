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
    buffer->max_size = CURL_RESPONSE_MAX_BYTES;
}

void curl_memory_buffer_free(struct curl_memory_buffer *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}

size_t curl_write_to_memory(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct curl_memory_buffer *mem = (struct curl_memory_buffer *)userp;

    // libcurl before 8.4 (Ubuntu 22.04 ships 7.81) applies CURLOPT_MAXFILESIZE
    // to an announced length only, so the limit is also enforced here as the
    // body arrives. Newer libcurl stops such a transfer itself, before this.
    if (mem->size > mem->max_size || realsize > mem->max_size - mem->size) {
        log_warn("Response body exceeds %zu bytes, transfer aborted", mem->max_size);
        return 0;
    }

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

CURL *curl_handle_new(void) {
    CURL *curl = curl_easy_init();
    if (curl && curl_easy_setopt(curl, CURLOPT_SSLVERSION,
                                 (long)CURL_SSLVERSION_TLSv1_2) != CURLE_OK) {
        curl_easy_cleanup(curl);
        curl = NULL;
    }
    return curl;
}

bool curl_setup_get(CURL *curl, const char *url, struct curl_memory_buffer *response) {
    return curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_URL, url) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE,
                            (curl_off_t)response->max_size) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_STRING) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK &&
           curl_install_thread_cancel(curl);
}

char* curl_url_encode(CURL *curl, const char *str) {
    if (!str) return NULL;
    return curl_easy_escape(curl, str, 0);
}
