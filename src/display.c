#define _POSIX_C_SOURCE 200809L

#include "display.h"
#include "bigfont.h"
#include "utf8.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static struct termios orig_termios;
static int initialized;
static volatile sig_atomic_t resize_flag;
static int tty_fd = -1;  /* /dev/tty for keyboard input (works even when stdin is a pipe) */
static int has_tty = 0;  /* 1 if we have an interactive terminal for input */

static int term_rows;
static int term_cols;

static void display_shutdown(void)
{
    if (!initialized) return;
    initialized = 0;
    tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);
    /* leave alt screen, show cursor */
    write(STDOUT_FILENO, "\033[?1049l\033[?25h", 14);
    if (tty_fd >= 0 && tty_fd != STDIN_FILENO) {
        close(tty_fd);
        tty_fd = -1;
    }
}

static void display_init(void)
{
    if (initialized) return;

    /* Open /dev/tty for keyboard input — works even when stdin is a pipe */
    tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd >= 0) {
        has_tty = 1;
    } else {
        /* No terminal available — use stdin, controls won't work */
        tty_fd = STDIN_FILENO;
        has_tty = 0;
    }

    tcgetattr(tty_fd, &orig_termios);

    struct termios raw = orig_termios;
    raw.c_iflag &= (unsigned)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (unsigned)~(OPOST);
    raw.c_lflag &= (unsigned)~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(tty_fd, TCSAFLUSH, &raw);

    initialized = 1;
    atexit(display_shutdown);

    /* alt screen, hide cursor */
    write(STDOUT_FILENO, "\033[?1049h\033[?25l", 14);
}

static void get_term_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        term_rows = ws.ws_row;
        term_cols = ws.ws_col;
    } else {
        term_rows = 24;
        term_cols = 80;
    }
}

void display_notify_resize(void)
{
    resize_flag = 1;
}

static int find_chapter(const ReaderText *text, int word_idx)
{
    int ch = 0;
    for (int i = 1; i < text->chapter_count; i++) {
        if (text->chapters[i].start_word <= word_idx)
            ch = i;
        else
            break;
    }
    return ch;
}

static int zoom_level = 2;  /* 1=small, 2=normal, 3=large, 4=extra large */

/* Spaced-out word: insert spaces between characters for zoom effect */
static int spaced_word(const char *word, char *out, int out_size, int spaces)
{
    int pos = 0;
    const char *p = word;
    int first = 1;
    while (*p && pos < out_size - 4) {
        if (!first) {
            for (int s = 0; s < spaces && pos < out_size - 1; s++)
                out[pos++] = ' ';
        }
        /* Copy one UTF-8 character */
        int byte_len = 1;
        unsigned char c = (unsigned char)*p;
        if (c >= 0xF0) byte_len = 4;
        else if (c >= 0xE0) byte_len = 3;
        else if (c >= 0xC0) byte_len = 2;
        for (int b = 0; b < byte_len && *p && pos < out_size - 1; b++)
            out[pos++] = *p++;
        first = 0;
    }
    out[pos] = '\0';
    return pos;
}

static int big_mode = 0;
static int context_mode = 0;

/* Render inline context: surrounding words dimmed on the same row as current word.
   Current word is bold white in the center, context words fade with distance. */
static void render_context_inline(char *buf, int *pos, int bufsize,
                                  const ReaderText *text, int word_idx,
                                  int row, int cols)
{
    int ctx = 5;
    int start = word_idx - ctx;
    if (start < 0) start = 0;
    int end = word_idx + ctx;
    if (end >= text->word_count) end = text->word_count - 1;

    /* First pass: compute display width of the composed line (no escape codes) */
    int total_display_width = 0;
    for (int i = start; i <= end; i++) {
        if (i > start) total_display_width++;  /* space */
        total_display_width += utf8_cplen(text->words[i]);
    }

    int col = (cols - total_display_width) / 2;
    if (col < 1) col = 1;

    /* Position cursor */
    *pos += snprintf(buf + *pos, (size_t)(bufsize - *pos),
                     "\033[%d;%dH", row, col);

    /* Second pass: render with colors */
    for (int i = start; i <= end && *pos < bufsize - 128; i++) {
        if (i > start) {
            buf[(*pos)++] = ' ';
        }

        const char *w = text->words[i];

        if (i == word_idx) {
            *pos += snprintf(buf + *pos, (size_t)(bufsize - *pos),
                             "\033[1;37m%s\033[0m", w);
        } else {
            *pos += snprintf(buf + *pos, (size_t)(bufsize - *pos),
                             "\033[2;90m%s\033[0m", w);
        }
    }
}

static void render_word_zoomed(char *buf, int *pos, int bufsize,
                               const char *word, int row, int cols)
{
    if (big_mode) {
        /* ASCII art block letters */
        char rows_out[BIGFONT_ROWS][512];
        bigfont_render(word, rows_out);
        int start_row = row - BIGFONT_ROWS / 2;
        if (start_row < 1) start_row = 1;
        for (int r = 0; r < BIGFONT_ROWS; r++) {
            int rlen = utf8_cplen(rows_out[r]);
            int col = (cols - rlen) / 2;
            if (col < 1) col = 1;
            *pos += snprintf(buf + *pos, (size_t)(bufsize - *pos),
                             "\033[%d;%dH\033[1;37m%s\033[0m",
                             start_row + r, col, rows_out[r]);
        }
        return;
    }

    if (zoom_level <= 1) {
        /* Small: dim, compact */
        int wlen = utf8_cplen(word);
        int col = (cols - wlen) / 2;
        if (col < 1) col = 1;
        *pos += snprintf(buf + *pos, (size_t)(bufsize - *pos),
                         "\033[%d;%dH\033[2;37m%s\033[0m", row, col, word);
    } else if (zoom_level == 2) {
        /* Normal: bold */
        int wlen = utf8_cplen(word);
        int col = (cols - wlen) / 2;
        if (col < 1) col = 1;
        *pos += snprintf(buf + *pos, (size_t)(bufsize - *pos),
                         "\033[%d;%dH\033[1;37m%s\033[0m", row, col, word);
    } else if (zoom_level == 3) {
        /* Large: bold, spaced out (1 space between chars) */
        char spaced[512];
        spaced_word(word, spaced, (int)sizeof(spaced), 1);
        int slen = utf8_cplen(spaced);
        int col = (cols - slen) / 2;
        if (col < 1) col = 1;
        *pos += snprintf(buf + *pos, (size_t)(bufsize - *pos),
                         "\033[%d;%dH\033[1;37m%s\033[0m", row, col, spaced);
    } else {
        /* Extra large: bold, wide spaced (2 spaces between chars) */
        char spaced[512];
        spaced_word(word, spaced, (int)sizeof(spaced), 2);
        int slen = utf8_cplen(spaced);
        int col = (cols - slen) / 2;
        if (col < 1) col = 1;
        *pos += snprintf(buf + *pos, (size_t)(bufsize - *pos),
                         "\033[%d;%dH\033[1;4;37m%s\033[0m", row, col, spaced);
    }
}

/* Sanitize chapter name: replace newlines/tabs with spaces, trim */
static void sanitize_name(const char *src, char *dst, int dst_size)
{
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 1; i++) {
        if (src[i] == '\n' || src[i] == '\r' || src[i] == '\t')
            dst[j++] = ' ';
        else
            dst[j++] = src[i];
    }
    /* Trim trailing spaces */
    while (j > 0 && dst[j - 1] == ' ') j--;
    dst[j] = '\0';
}

/* Render chapter selection overlay. Returns selected chapter index or -1 if cancelled. */
static int render_chapter_select(const ReaderText *text, int current_chapter)
{
    if (text->chapter_count <= 0) return -1;

    int selected = current_chapter;
    int scroll_offset = 0;

    for (;;) {
        char buf[8192];
        int pos = 0;
        int rows = term_rows;
        int cols = term_cols;

        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "\033[2J\033[H");

        /* Title */
        const char *title = "Chapters  (\xe2\x86\x91\xe2\x86\x93 navigate, Enter: jump, Esc: back)";
        int tcol = (cols - utf8_cplen(title)) / 2;
        if (tcol < 1) tcol = 1;
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "\033[2;%dH\033[1;36m%s\033[0m", tcol, title);

        /* List */
        int list_rows = rows - 6;
        if (list_rows < 1) list_rows = 1;

        /* Adjust scroll to keep selected visible */
        if (selected < scroll_offset) scroll_offset = selected;
        if (selected >= scroll_offset + list_rows)
            scroll_offset = selected - list_rows + 1;

        int max_name = cols - 14;
        if (max_name < 10) max_name = 10;

        for (int i = 0; i < list_rows && scroll_offset + i < text->chapter_count; i++) {
            int ci = scroll_offset + i;
            int row = 4 + i;
            int pct = text->word_count > 0
                ? text->chapters[ci].start_word * 100 / text->word_count : 0;

            char clean[FR_MAX_CHAPTER_NAME];
            sanitize_name(text->chapters[ci].name, clean, (int)sizeof(clean));

            /* Truncate to fit */
            if (utf8_cplen(clean) > max_name) {
                int cp = 0;
                char *p = clean;
                while (*p && cp < max_name - 3) {
                    if (((unsigned char)*p & 0xC0) != 0x80) cp++;
                    p++;
                }
                *p++ = '.'; *p++ = '.'; *p++ = '.'; *p = '\0';
            }

            if (ci == selected) {
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                                "\033[%d;3H\033[1;7;37m > %s\033[0m\033[%d;%dH\033[1;7;37m%3d%%\033[0m",
                                row, clean, row, cols - 5, pct);
            } else {
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                                "\033[%d;3H   \033[37m%s\033[0m\033[%d;%dH\033[2m%3d%%\033[0m",
                                row, clean, row, cols - 5, pct);
            }
        }

        write(STDOUT_FILENO, buf, (size_t)pos);

        /* Read key */
        struct pollfd pfd = {.fd = tty_fd, .events = POLLIN};
        int ret = poll(&pfd, 1, -1);
        if (ret <= 0) continue;

        char ch;
        if (read(tty_fd, &ch, 1) != 1) continue;

        if (ch == '\033') {
            char seq[2];
            if (read(tty_fd, seq, 2) == 2 && seq[0] == '[') {
                if (seq[1] == 'A' && selected > 0) selected--;
                if (seq[1] == 'B' && selected < text->chapter_count - 1) selected++;
            } else {
                /* Bare Esc — cancel */
                return -1;
            }
        } else if (ch == '\r' || ch == '\n') {
            return selected;
        } else if (ch == 'q' || ch == 'c') {
            return -1;
        }
    }
}

static void render_frame(const char *word, int word_idx,
                         const ReaderText *text, int wpm, int paused)
{
    char buf[4096];
    int pos = 0;
    int rows = term_rows;
    int cols = term_cols;

    /* clear screen */
    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "\033[2J\033[H");

    /* paused indicator above word */
    if (paused) {
        const char *label = "[PAUSED]";
        int llen = 8;
        int pcol = (cols - llen) / 2;
        if (pcol < 1) pcol = 1;
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "\033[%d;%dH\033[1;33m%s\033[0m",
                        rows / 2 - 3, pcol, label);
    }

    if (context_mode && !big_mode) {
        /* Inline context: words on same row, current word highlighted */
        render_context_inline(buf, &pos, (int)sizeof(buf), text, word_idx,
                              rows / 2, cols);
    } else {
        /* Single centered word with zoom */
        render_word_zoomed(buf, &pos, (int)sizeof(buf), word, rows / 2, cols);
        /* Context below big text */
        if (context_mode && big_mode) {
            render_context_inline(buf, &pos, (int)sizeof(buf), text, word_idx,
                                  rows / 2 + 4, cols);
        }
    }

    /* paused hints below word */
    if (paused) {
        const char *hints = "Space: resume  +/-: zoom  c: chapters  q: quit";
        int hlen = utf8_cplen(hints);
        int hcol = (cols - hlen) / 2;
        if (hcol < 1) hcol = 1;
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "\033[%d;%dH\033[2m%s\033[0m",
                        rows / 2 + 3, hcol, hints);
    }

    /* progress bar at rows - 1 */
    {
        int progress = text->word_count > 0
            ? word_idx * 100 / text->word_count : 0;
        if (progress > 100) progress = 100;

        int bar_width = cols - 20;
        if (bar_width < 4) bar_width = 4;
        int filled = bar_width * progress / 100;

        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "\033[%d;1H  \033[36m", rows - 1);

        for (int i = 0; i < filled && pos + 3 < (int)sizeof(buf); i++) {
            buf[pos++] = '\xe2';
            buf[pos++] = '\x96';
            buf[pos++] = '\x88';
        }
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "\033[0m");
        for (int i = filled; i < bar_width && pos + 3 < (int)sizeof(buf); i++) {
            buf[pos++] = '\xe2';
            buf[pos++] = '\x96';
            buf[pos++] = '\x91';
        }

        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "  \033[37m%3d%%\033[0m  \033[33m%dwpm\033[0m",
                        progress, wpm);
    }

    /* chapter name at last row */
    if (text->chapter_count > 0) {
        int ch = find_chapter(text, word_idx);
        const char *name = text->chapters[ch].name;
        int nlen = utf8_cplen(name);
        int max = cols - 2;
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "\033[%d;2H\033[2m", rows);
        /* truncate to cols */
        if (nlen <= max) {
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s", name);
        } else {
            /* byte-copy up to max codepoints */
            int cp = 0;
            const char *p = name;
            while (*p && cp < max) {
                if (((unsigned char)*p & 0xC0) != 0x80) cp++;
                if (cp <= max) {
                    if (pos < (int)sizeof(buf) - 1)
                        buf[pos++] = *p;
                }
                p++;
            }
        }
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "\033[0m");
    }

    write(STDOUT_FILENO, buf, (size_t)pos);
}

static int compute_delay_ms(const char *word, int wpm, int scale_enabled)
{
    double base = 60000.0 / wpm;
    if (!scale_enabled) return (int)base;

    int cplen = utf8_cplen(word);
    double scale = 1.0;
    if (cplen >= 5 && cplen <= 8)       scale = 1.2;
    else if (cplen >= 9 && cplen <= 12) scale = 1.4;
    else if (cplen > 12)                scale = 1.6;

    size_t len = strlen(word);
    if (len > 0) {
        char last = word[len - 1];
        if (last == '.' || last == '!' || last == '?' ||
            last == ':' || last == ';')
            scale *= 1.5;
    }

    return (int)(base * scale);
}

static long elapsed_ms(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000L +
           (now.tv_nsec - start->tv_nsec) / 1000000L;
}

int display_run(const ReaderText *text, const DisplayOpts *opts)
{
    display_init();
    get_term_size();

    int word_idx = opts->start_word;
    int wpm = opts->wpm;
    int paused = 0;
    big_mode = opts->big_mode;
    context_mode = opts->context_mode;

    while (word_idx < text->word_count) {
        render_frame(text->words[word_idx], word_idx, text, wpm, paused);

        int delay = paused ? -1 :
            compute_delay_ms(text->words[word_idx], wpm, opts->scale_enabled);

        if (!has_tty) {
            /* No interactive terminal — just sleep for the delay */
            if (!paused && delay > 0) {
                struct timespec ts = {.tv_sec = delay / 1000,
                                      .tv_nsec = (delay % 1000) * 1000000L};
                nanosleep(&ts, NULL);
            }
        } else {
            struct timespec start;
            clock_gettime(CLOCK_MONOTONIC, &start);
            int remaining = delay;

            for (;;) {
                struct pollfd pfd = {.fd = tty_fd, .events = POLLIN};
                int ret = poll(&pfd, 1, remaining);

                if (ret < 0) {
                    if (errno == EINTR) {
                        if (resize_flag) {
                            resize_flag = 0;
                            get_term_size();
                            render_frame(text->words[word_idx], word_idx,
                                         text, wpm, paused);
                        }
                        if (!paused && remaining > 0) {
                            long e = elapsed_ms(&start);
                            remaining = delay - (int)e;
                            if (remaining < 0) remaining = 0;
                        }
                        continue;
                    }
                    break; /* unexpected error */
                }

                if (ret > 0 && (pfd.revents & POLLIN)) {
                    char ch;
                    if (read(tty_fd, &ch, 1) != 1) break;

                    if (ch == 'q') goto done;
                    if (ch == ' ') {
                        paused = !paused;
                        break; /* re-render */
                    }
                    if (ch == '+' || ch == '=') {
                        if (zoom_level < 4) zoom_level++;
                        break;
                    }
                    if (ch == '-' || ch == '_') {
                        if (zoom_level > 1) zoom_level--;
                        break;
                    }
                    if (ch == 'c' && text->chapter_count > 1) {
                        paused = 1;
                        int cur_ch = find_chapter(text, word_idx);
                        int sel = render_chapter_select(text, cur_ch);
                        if (sel >= 0 && sel < text->chapter_count) {
                            word_idx = text->chapters[sel].start_word;
                        }
                        break; /* re-render */
                    }
                    if (ch == '\033') {
                        char seq[2];
                        if (read(tty_fd, seq, 2) == 2 && seq[0] == '[') {
                            switch (seq[1]) {
                            case 'A': /* up — faster */
                                wpm += 50;
                                if (wpm > 1000) wpm = 1000;
                                break;
                            case 'B': /* down — slower */
                                wpm -= 50;
                                if (wpm < 50) wpm = 50;
                                break;
                            case 'C': /* right — skip forward */
                                word_idx += 5;
                                if (word_idx >= text->word_count)
                                    word_idx = text->word_count - 1;
                                break;
                            case 'D': /* left — skip back */
                                word_idx -= 5;
                                if (word_idx < 0) word_idx = 0;
                                break;
                            }
                            break; /* re-render */
                        }
                    }
                    /* unknown key — re-render */
                    break;
                }

                /* poll timeout: delay expired */
                if (!paused) break;
            }
        }

        if (!paused) word_idx++;
    }

done:
    display_shutdown();
    return word_idx;
}
