#ifndef FR_READER_H
#define FR_READER_H
#include <stddef.h>

#define FR_MAX_CHAPTER_NAME 256

typedef struct {
    char name[FR_MAX_CHAPTER_NAME];
    int start_word;
} Chapter;

typedef struct {
    char **words;
    int word_count;
    Chapter *chapters;
    int chapter_count;
} ReaderText;

int reader_load_file(const char *path, const char *chapter_name, ReaderText *out);
int reader_load_stdin(ReaderText *out);
int reader_load_buffer(const char *text, size_t len, const char *chapter_name, ReaderText *out);
void reader_set_chapters(ReaderText *rt, Chapter *chapters, int count);
void reader_free(ReaderText *rt);

#endif
