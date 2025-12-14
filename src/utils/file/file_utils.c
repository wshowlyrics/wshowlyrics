#include "file_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <errno.h>

#define CACHE_BASE_DIR "/tmp/wshowlyrics"
#define CACHE_ALBUM_ART_DIR CACHE_BASE_DIR "/album_art"
#define CACHE_LYRICS_DIR CACHE_BASE_DIR "/lyrics"
#define CACHE_TRANSLATED_DIR CACHE_BASE_DIR "/translated"

bool calculate_file_md5(const char *filepath, char *checksum_out) {
    if (!filepath || !checksum_out) {
        return false;
    }

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return false;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fclose(file);
        return false;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1) {
        EVP_MD_CTX_free(mdctx);
        fclose(file);
        return false;
    }

    unsigned char buffer[8192];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, bytes_read) != 1) {
            EVP_MD_CTX_free(mdctx);
            fclose(file);
            return false;
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        fclose(file);
        return false;
    }

    EVP_MD_CTX_free(mdctx);
    fclose(file);

    // Convert to hex string
    for (unsigned int i = 0; i < hash_len; i++) {
        sprintf(checksum_out + (i * 2), "%02x", hash[i]);
    }
    checksum_out[hash_len * 2] = '\0';

    return true;
}

bool file_has_changed(const char *filepath, const char *expected_checksum) {
    if (!filepath || !expected_checksum) {
        return false;
    }

    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return false; // File doesn't exist
    }

    char current_checksum[MD5_DIGEST_STRING_LENGTH];
    if (!calculate_file_md5(filepath, current_checksum)) {
        return false;
    }

    return strcmp(current_checksum, expected_checksum) != 0;
}

int build_path(char *dest, size_t dest_size, const char *fmt, ...) {
    if (!dest || !fmt || dest_size == 0) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(dest, dest_size, fmt, args);
    va_end(args);

    // Check for error or truncation
    if (written < 0 || written >= (int)dest_size) {
        return -1;
    }

    return written;
}

static bool mkdir_p(const char *path) {
    struct stat st;

    // Check if directory already exists
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        // Path exists but is not a directory
        return false;
    }

    // Create directory with mode 0755
    if (mkdir(path, 0755) == 0) {
        return true;
    }

    // Failed to create, but errno == EEXIST means another process created it
    return (errno == EEXIST);
}

bool ensure_cache_directories(void) {
    // Create base directory
    if (!mkdir_p(CACHE_BASE_DIR)) {
        return false;
    }

    // Create subdirectories
    if (!mkdir_p(CACHE_ALBUM_ART_DIR)) {
        return false;
    }

    if (!mkdir_p(CACHE_LYRICS_DIR)) {
        return false;
    }

    if (!mkdir_p(CACHE_TRANSLATED_DIR)) {
        return false;
    }

    return true;
}

int build_album_art_cache_path(char *dest, size_t dest_size, const char *md5_hash) {
    if (!dest || !md5_hash || dest_size == 0) {
        return -1;
    }

    return build_path(dest, dest_size, "%s/%s.png", CACHE_ALBUM_ART_DIR, md5_hash);
}

int build_lyrics_cache_path(char *dest, size_t dest_size, const char *md5_hash) {
    if (!dest || !md5_hash || dest_size == 0) {
        return -1;
    }

    return build_path(dest, dest_size, "%s/%s.lrc", CACHE_LYRICS_DIR, md5_hash);
}

bool calculate_metadata_md5(const char *artist, const char *title, const char *album, char *md5_out) {
    if (!md5_out) {
        return false;
    }

    // Combine metadata into single string: "artist|title|album"
    // Use | as separator to avoid conflicts with common metadata characters
    char metadata[1024];
    snprintf(metadata, sizeof(metadata), "%s|%s|%s",
             artist ? artist : "",
             title ? title : "",
             album ? album : "");

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        return false;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1) {
        EVP_MD_CTX_free(mdctx);
        return false;
    }

    if (EVP_DigestUpdate(mdctx, metadata, strlen(metadata)) != 1) {
        EVP_MD_CTX_free(mdctx);
        return false;
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        return false;
    }

    EVP_MD_CTX_free(mdctx);

    // Convert to hex string
    for (unsigned int i = 0; i < hash_len; i++) {
        sprintf(md5_out + (i * 2), "%02x", hash[i]);
    }
    md5_out[hash_len * 2] = '\0';

    return true;
}

int build_translation_cache_path(char *dest, size_t dest_size,
                                  const char *original_md5,
                                  const char *target_lang) {
    if (!dest || !original_md5 || !target_lang || dest_size == 0) {
        return -1;
    }

    return build_path(dest, dest_size, "%s/%s_%s.json",
                      CACHE_TRANSLATED_DIR, original_md5, target_lang);
}
