#ifndef STATE_HELPERS_H
#define STATE_HELPERS_H

#include <stdint.h>

/**
 * Escape newlines in text for logging
 * Converts: LF (\n) -> ^J, CRLF (\r\n) -> ^M^J
 *
 * @param text Text to escape
 * @return Newly allocated string, caller must free. NULL if text is NULL or allocation fails.
 */
char* state_helpers_escape_newlines(const char *text);

/**
 * Parse color string to uint32_t
 * Accepts formats: #RRGGBB or #RRGGBBAA
 *
 * @param color Color string (with or without leading #)
 * @return Color in 0xRRGGBBAA format. Returns 0xFFFFFFFF on error.
 */
uint32_t state_helpers_parse_color(const char *color);

#endif // STATE_HELPERS_H
