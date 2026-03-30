#define _GNU_SOURCE
#include "progress.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME  0x100000001b3ULL
#define RECORD_SIZE 12

uint64_t progress_hash(const void *data, size_t len)
{
    uint64_t h = FNV_OFFSET;
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

/* Create directory path recursively (like mkdir -p). */
static int mkdirs(const char *path)
{
    char tmp[4096];
    size_t len = strlen(path);
    if (len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int build_path(char *buf, size_t bufsz)
{
    const char *home = getenv("HOME");
    if (!home)
        return -1;
    int n = snprintf(buf, bufsz, "%s/.config/flashreader/positions.dat", home);
    if (n < 0 || (size_t)n >= bufsz)
        return -1;
    return 0;
}

static void write_u64_le(unsigned char *dst, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        dst[i] = (unsigned char)(v >> (i * 8));
}

static void write_i32_le(unsigned char *dst, int32_t v)
{
    uint32_t u = (uint32_t)v;
    for (int i = 0; i < 4; i++)
        dst[i] = (unsigned char)(u >> (i * 8));
}

static uint64_t read_u64_le(const unsigned char *src)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)src[i] << (i * 8);
    return v;
}

static int32_t read_i32_le(const unsigned char *src)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= (uint32_t)src[i] << (i * 8);
    return (int32_t)v;
}

int progress_save(uint64_t hash, int word_index)
{
    char path[4096];
    if (build_path(path, sizeof(path)) != 0)
        return -1;

    /* Ensure directory exists. Trim filename to get dir. */
    char dir[4096];
    memcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash)
        *slash = '\0';
    if (mkdirs(dir) != 0)
        return -1;

    /* Read existing records. */
    unsigned char *records = NULL;
    size_t count = 0;

    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz % RECORD_SIZE == 0) {
            records = malloc((size_t)sz);
            if (records) {
                if (fread(records, 1, (size_t)sz, f) == (size_t)sz)
                    count = (size_t)sz / RECORD_SIZE;
                else {
                    free(records);
                    records = NULL;
                    count = 0;
                }
            }
        }
        fclose(f);
    }

    /* Scan for existing hash. */
    int found = 0;
    for (size_t i = 0; i < count; i++) {
        if (read_u64_le(records + i * RECORD_SIZE) == hash) {
            write_i32_le(records + i * RECORD_SIZE + 8, (int32_t)word_index);
            found = 1;
            break;
        }
    }

    if (!found) {
        size_t new_size = (count + 1) * RECORD_SIZE;
        unsigned char *tmp = realloc(records, new_size);
        if (!tmp) {
            free(records);
            return -1;
        }
        records = tmp;
        write_u64_le(records + count * RECORD_SIZE, hash);
        write_i32_le(records + count * RECORD_SIZE + 8, (int32_t)word_index);
        count++;
    }

    f = fopen(path, "wb");
    if (!f) {
        free(records);
        return -1;
    }
    size_t written = fwrite(records, 1, count * RECORD_SIZE, f);
    fclose(f);
    free(records);
    return (written == count * RECORD_SIZE) ? 0 : -1;
}

int progress_load(uint64_t hash)
{
    char path[4096];
    if (build_path(path, sizeof(path)) != 0)
        return -1;

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz % RECORD_SIZE != 0) {
        fclose(f);
        return -1;
    }

    size_t count = (size_t)sz / RECORD_SIZE;
    unsigned char *records = malloc((size_t)sz);
    if (!records) {
        fclose(f);
        return -1;
    }
    if (fread(records, 1, (size_t)sz, f) != (size_t)sz) {
        free(records);
        fclose(f);
        return -1;
    }
    fclose(f);

    for (size_t i = 0; i < count; i++) {
        if (read_u64_le(records + i * RECORD_SIZE) == hash) {
            int32_t idx = read_i32_le(records + i * RECORD_SIZE + 8);
            free(records);
            return (int)idx;
        }
    }

    free(records);
    return -1;
}
