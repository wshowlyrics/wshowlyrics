#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "parser/srt/srt_parser.h"
#include "parser/lrc/lrc_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Limit input size to avoid timeouts
    if (size > 65536) return 0;

    // Null-terminate the input
    char *input = malloc(size + 1);
    if (!input) return 0;
    memcpy(input, data, size);
    input[size] = '\0';

    struct lyrics_data lyrics = {0};
    if (srt_parse_string(input, &lyrics)) {
        lrc_free_data(&lyrics);
    }

    free(input);
    return 0;
}
