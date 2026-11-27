// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _STRING_UTILS_H
#define _STRING_UTILS_H

#include <stdbool.h>
#include <stddef.h>

// Whether a string is there and not empty
static inline bool str_is_set(const char *s) {
    return s && s[0] != '\0';
}

// Cut a trailing audio/video file extension (".mkv", ".flac", ...) off name, in
// place. Case-insensitive. NULL-safe.
//
// For names that are not known to be file names: a track title is a file name
// only when the file has no title tag, and a real title may contain a dot
// ("Mr.Children", "Ver.2"), so its tail is cut only when it is a known
// extension. A name that is nothing but an extension is left alone.
void strip_media_extension(char *name);

// Cut the extension off a file name, in place. NULL-safe. Unlike a title, a
// file name's last dot does start its extension, whatever the format, so no
// list is consulted — one left "song.mka" searching for "song.mka.lrc". The
// tail must still look like an extension (a few letters or digits), so a file
// saved without one, such as "Mr.Children - Song", keeps its name.
void strip_file_extension(char *filename);

// Sanitize title by removing bracketed parts such as a YouTube ID or quality tag.
// The media file extension is already gone: mpris.c cuts it when it reads the title.
// Example: "[4K60FPS] Song Title [AbCdEfG1234]" -> "Song Title"
// Caller must free the returned string
char* sanitize_title(const char *title);

// Trim leading and trailing whitespace from string (in-place)
// Returns pointer to trimmed string (within original buffer)
// NULL-safe: returns NULL if input is NULL
char* trim_whitespace(char *str);

#endif
