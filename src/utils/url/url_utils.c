#include "url_utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// URL decode a string (handles %XX encoding)
char* url_decode_string(const char *str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    char *decoded = malloc(len + 1);
    if (!decoded) return NULL;

    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        if (str[i] == '%' && i + 2 < len &&
            isxdigit((unsigned char)str[i+1]) && isxdigit((unsigned char)str[i+2])) {
            // Decode %XX. Both digits are validated as hex above, so strtol
            // cannot skip whitespace or consume a sign ("%-1", "% 1", "%+A"
            // would otherwise decode to bogus bytes instead of staying literal).
            char hex[3] = { str[i+1], str[i+2], '\0' };
            long val = strtol(hex, NULL, 16);
            // Reject %00: decoding it to a NUL would truncate the path/filename
            // (CWE-158). Leave the literal "%00" so the lookup simply fails
            // instead of silently searching a shortened path.
            if (val != 0) {
                decoded[j++] = (char)val;
                i += 3;
                continue;
            }
        }
        decoded[j++] = str[i++];
    }
    decoded[j] = '\0';

    return decoded;
}
