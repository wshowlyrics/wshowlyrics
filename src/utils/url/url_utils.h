#ifndef URL_UTILS_H
#define URL_UTILS_H

// Decode a percent-encoded string (%XX). The result is a newly allocated,
// NUL-terminated string the caller must free, or NULL on allocation failure
// or NULL input.
//
// %00 is intentionally NOT decoded to a NUL byte: doing so would truncate a
// path/filename (CWE-158). The literal "%00" is preserved instead so an
// attacker-controlled URI cannot silently shorten the search path.
char* url_decode_string(const char *str);

#endif // URL_UTILS_H
