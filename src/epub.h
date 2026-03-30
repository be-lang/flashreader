#ifndef FR_EPUB_H
#define FR_EPUB_H
#include "reader.h"

/* Extract text and chapters from an EPUB file.
   Returns 0 on success, -1 on error. */
int epub_load(const char *path, ReaderText *out);

/* Strip HTML tags and return plain text. Caller frees result.
   chapters_out and chapter_count_out receive detected chapters. */
char *html_strip(const char *html, size_t len,
                 Chapter **chapters_out, int *chapter_count_out,
                 int word_offset);

#endif
