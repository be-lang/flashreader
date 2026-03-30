#define _POSIX_C_SOURCE 200809L
#include "reader.h"
#include "epub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int reader_load_buffer(const char *text, size_t len, const char *chapter_name,
                       ReaderText *out)
{
    memset(out, 0, sizeof(*out));

    int cap = 1024;
    out->words = malloc((size_t)cap * sizeof(char *));
    if (!out->words)
        return -1;

    size_t i = 0;
    while (i < len) {
        /* skip whitespace */
        while (i < len && is_ws(text[i]))
            i++;
        if (i >= len)
            break;

        /* start of word */
        size_t start = i;
        while (i < len && !is_ws(text[i]))
            i++;
        size_t wlen = i - start;

        if (out->word_count >= cap) {
            cap *= 2;
            char **tmp = realloc(out->words, (size_t)cap * sizeof(char *));
            if (!tmp) {
                reader_free(out);
                return -1;
            }
            out->words = tmp;
        }

        out->words[out->word_count] = strndup(text + start, wlen);
        if (!out->words[out->word_count]) {
            reader_free(out);
            return -1;
        }
        out->word_count++;
    }

    /* create single chapter */
    out->chapters = malloc(sizeof(Chapter));
    if (!out->chapters) {
        reader_free(out);
        return -1;
    }
    out->chapter_count = 1;
    memset(out->chapters, 0, sizeof(Chapter));
    if (chapter_name)
        snprintf(out->chapters[0].name, FR_MAX_CHAPTER_NAME, "%s", chapter_name);
    out->chapters[0].start_word = 0;

    return 0;
}

static int ends_with_epub(const char *path)
{
    size_t len = strlen(path);
    if (len < 5) return 0;
    const char *ext = path + len - 5;
    return (tolower((unsigned char)ext[0]) == '.' &&
            tolower((unsigned char)ext[1]) == 'e' &&
            tolower((unsigned char)ext[2]) == 'p' &&
            tolower((unsigned char)ext[3]) == 'u' &&
            tolower((unsigned char)ext[4]) == 'b');
}

int reader_load_file(const char *path, const char *chapter_name, ReaderText *out)
{
    if (ends_with_epub(path))
        return epub_load(path, out);

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);

    char *buf = malloc((size_t)sz);
    if (!buf && sz > 0) {
        fclose(f);
        return -1;
    }

    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    int ret = reader_load_buffer(buf, (size_t)sz, chapter_name, out);
    free(buf);
    return ret;
}

int reader_load_stdin(ReaderText *out)
{
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return -1;

    for (;;) {
        size_t n = fread(buf + len, 1, cap - len, stdin);
        len += n;
        if (n == 0)
            break;
        if (len == cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                return -1;
            }
            buf = tmp;
        }
    }

    int ret = reader_load_buffer(buf, len, "stdin", out);
    free(buf);
    return ret;
}

void reader_set_chapters(ReaderText *rt, Chapter *chapters, int count)
{
    free(rt->chapters);
    rt->chapters = chapters;
    rt->chapter_count = count;
}

void reader_free(ReaderText *rt)
{
    for (int i = 0; i < rt->word_count; i++)
        free(rt->words[i]);
    free(rt->words);
    free(rt->chapters);
    memset(rt, 0, sizeof(*rt));
}
