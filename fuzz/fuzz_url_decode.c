#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "utils/url/url_utils.h"

// Fuzz the percent-decoder that runs on MPRIS file:// URIs (attacker-influenced
// input from arbitrary session-bus players). Exercises buffer bounds and the
// %00 handling added for SEC-4.
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 65536) return 0;

    char *input = malloc(size + 1);
    if (!input) return 0;
    memcpy(input, data, size);
    input[size] = '\0';

    char *decoded = url_decode_string(input);
    free(decoded);

    free(input);
    return 0;
}
