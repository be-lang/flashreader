#ifndef FR_BIGFONT_H
#define FR_BIGFONT_H

#define BIGFONT_ROWS 5

/* Render a word in big block letters into a buffer.
   Returns the number of rows written (always BIGFONT_ROWS).
   Each row is null-terminated in rows_out[0..4].
   rows_out must be arrays of at least max_cols+1 chars each. */
int bigfont_render(const char *word, char rows_out[BIGFONT_ROWS][512]);

#endif
