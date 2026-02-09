#ifndef _LYRICS_RUNTIME_DIR_H
#define _LYRICS_RUNTIME_DIR_H

// Get runtime directory path for wshowlyrics
// Returns "$XDG_RUNTIME_DIR/wshowlyrics" or NULL if XDG_RUNTIME_DIR is not set
// Creates the directory on first call (mode 0700)
// Returns static buffer (do not free)
const char* get_runtime_dir(void);

#endif // _LYRICS_RUNTIME_DIR_H
