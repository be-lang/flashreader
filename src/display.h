#ifndef FR_DISPLAY_H
#define FR_DISPLAY_H
#include "reader.h"

typedef struct {
    int wpm;            /* words per minute (50-1000) */
    int scale_enabled;  /* word-length time scaling */
    int start_word;     /* word index to start from */
    int big_mode;       /* render words as ASCII art block letters */
    int context_mode;   /* show surrounding words dimmed around current word */
} DisplayOpts;

/* Run the RSVP display loop. Returns final word index on exit. */
int display_run(const ReaderText *text, const DisplayOpts *opts);

/* Set resize flag — call from SIGWINCH handler. */
void display_notify_resize(void);

#endif
