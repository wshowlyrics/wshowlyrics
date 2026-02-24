#include "lrc_common.h"
#include "../utils/parser_utils.h"
#include <stdlib.h>
#include <string.h>

void lrc_free_data(struct lyrics_data *data) {
    if (!data) {
        return;
    }

    free(data->metadata.title);
    free(data->metadata.artist);
    free(data->metadata.album);
    free(data->source_file_path);

    struct lyrics_line *line = data->lines;
    while (line) {
        struct lyrics_line *next = line->next;
        free(line->text);
        free(line->translation);

        // Free ruby segments if present (LRC format)
        free_ruby_segments(line->ruby_segments);

        // Free word segments if present (LRCX format)
        free_word_segments(line->segments);

        free(line);
        line = next;
    }

    memset(data, 0, sizeof(struct lyrics_data));
}

struct lyrics_line* lrc_find_line_at_time(struct lyrics_data *data, int64_t timestamp_us) {
    if (!data || !data->lines) {
        return NULL;
    }

    struct lyrics_line *current = NULL;
    struct lyrics_line *line = data->lines;

    while (line) {
        if (line->timestamp_us > timestamp_us) {
            break;
        }
        current = line;
        line = line->next;
    }

    return current;
}

int lrc_get_line_index(const struct lyrics_data *data, const struct lyrics_line *target) {
    if (!data || !target) {
        return -1;
    }

    int index = 0;
    const struct lyrics_line *line = data->lines;

    while (line) {
        if (line == target) {
            return index;
        }
        line = line->next;
        index++;
    }

    return -1;
}
