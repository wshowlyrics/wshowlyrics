#include "file_utils.h"
#include "../../constants.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <openssl/evp.h>
#include <sys/stat.h>

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
