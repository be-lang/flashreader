#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "reader.h"
#include "display.h"
#include "progress.h"

static void print_help(void)
{
    printf(
        "flashreader — terminal speed reader\n"
        "\n"
        "Usage: flashreader [options] [file]\n"
        "\n"
        "Options:\n"
        "  --wpm <N>      Words per minute (default: 300, range: 50-1000)\n"
        "  --scale-speed  Slow down for longer words and punctuation\n"
        "  --big          Display words as large block letters\n"
        "  --context      Show surrounding words dimmed around current word\n"
        "  -h, --help     Show this help\n"
        "\n"
        "Controls:\n"
        "  Space          Pause / resume\n"
        "  ← →            Skip back / forward 5 words\n"
        "  ↑ ↓            Adjust WPM ±50\n"
        "  + / -          Zoom in / out\n"
        "  c              Chapter list (when paused, EPUB only)\n"
        "  q              Quit (saves position)\n"
        "\n"
        "Supports .txt, .epub, and stdin (piped input).\n"
    );
}

static void on_resize(int sig)
{
    (void)sig;
    display_notify_resize();
}

static const char *file_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int main(int argc, char **argv)
{
    int wpm = 300;
    int scale = 0;
    int big = 0;
    int context = 0;
    const char *filename = NULL;

    /* --- arg parsing --- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "--wpm") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --wpm requires a value\n");
                return 1;
            }
            wpm = atoi(argv[++i]);
            if (wpm < 50 || wpm > 1000) {
                fprintf(stderr, "Error: WPM must be between 50 and 1000\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--scale-speed") == 0) {
            scale = 1;
        } else if (strcmp(argv[i], "--big") == 0) {
            big = 1;
        } else if (strcmp(argv[i], "--context") == 0) {
            context = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        } else {
            filename = argv[i];
        }
    }

    /* --- determine input source --- */
    int from_stdin = 0;
    if (!filename && !isatty(STDIN_FILENO)) {
        from_stdin = 1;
    } else if (!filename) {
        fprintf(stderr, "Error: no input file\n\n");
        print_help();
        return 1;
    }

    /* --- load text --- */
    ReaderText text = {0};
    if (from_stdin) {
        if (reader_load_stdin(&text) < 0) {
            fprintf(stderr, "Error: failed to read from stdin\n");
            return 1;
        }
    } else {
        if (reader_load_file(filename, file_basename(filename), &text) < 0) {
            fprintf(stderr, "Error: failed to load '%s'\n", filename);
            return 1;
        }
    }

    /* --- resume support (file input only) --- */
    uint64_t hash = 0;
    int start_word = 0;

    if (!from_stdin) {
        FILE *f = fopen(filename, "rb");
        if (f) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf), f);
            fclose(f);
            if (n > 0) {
                hash = progress_hash(buf, n);
                int saved = progress_load(hash);
                if (saved >= 0 && saved < text.word_count) {
                    int pct = (saved * 100) / text.word_count;
                    printf("Resume from %d%%? [Y/n] ", pct);
                    fflush(stdout);
                    int ch = getchar();
                    if (ch == 'n' || ch == 'N') {
                        start_word = 0;
                    } else {
                        start_word = saved;
                    }
                }
            }
        }
    }

    /* --- set up SIGWINCH --- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_resize;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART */
    sigaction(SIGWINCH, &sa, NULL);

    /* --- run display --- */
    DisplayOpts opts = {
        .wpm = wpm,
        .scale_enabled = scale,
        .big_mode = big,
        .context_mode = context,
        .start_word = start_word
    };
    int final_word = display_run(&text, &opts);

    /* --- save position (file input only) --- */
    if (!from_stdin && hash != 0) {
        progress_save(hash, final_word);
    }

    /* --- done message --- */
    if (final_word >= text.word_count) {
        printf("Done! %d words.\n", text.word_count);
    }

    reader_free(&text);
    return 0;
}
