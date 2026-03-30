# flashreader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a terminal speed reader that displays one word at a time with adjustable speed, supporting plain text, EPUB, and stdin.

**Architecture:** Five C modules (reader, epub, display, progress, main) in a single-threaded event loop. EPUB parsing requires a minimal ZIP reader and RFC 1951 inflate. Display uses raw ANSI escape codes with poll()-based timing.

**Tech Stack:** C11, POSIX termios, ANSI escape codes. Zero dependencies.

**Spec:** `docs/design.md`

---

## File Structure

```
flashreader/
├── Makefile
├── README.md
├── LICENSE
├── src/
│   ├── main.c          — Entry point, arg parsing, orchestration
│   ├── reader.c         — Text loading, word tokenization
│   ├── reader.h         — Reader types and API
│   ├── epub.c           — EPUB/ZIP parsing, HTML stripping
│   ├── epub.h           — EPUB API
│   ├── inflate.c        — RFC 1951 deflate decompression
│   ├── inflate.h        — Inflate API
│   ├── display.c        — Terminal rendering, timing loop, controls
│   ├── display.h        — Display API
│   ├── progress.c       — Position save/load, FNV-1a hash
│   ├── progress.h       — Progress API
│   └── utf8.c           — UTF-8 codepoint counting
│   └── utf8.h           — UTF-8 helpers
└── tests/
    ├── test_main.c      — Test runner
    ├── test_inflate.c   — Inflate unit tests
    ├── test_reader.c    — Tokenizer tests
    └── test_progress.c  — Hash + save/load tests
```

---

### Task 1: Project scaffold

**Files:**
- Create: `Makefile`, `src/main.c`, `.gitignore`, `LICENSE`

- [ ] **Step 1: Create .gitignore**

```
build/
*.o
```

- [ ] **Step 2: Create MIT LICENSE** (Copyright 2026 Benjamin Lang)

- [ ] **Step 3: Create minimal main.c**

```c
#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("flashreader — terminal speed reader\n");
    return 0;
}
```

- [ ] **Step 4: Create Makefile**

```makefile
CC ?= gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O2 -Isrc
BUILDDIR = build

SOURCES = src/main.c
TARGET = $(BUILDDIR)/flashreader

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) -lm

clean:
	rm -rf $(BUILDDIR)
```

- [ ] **Step 5: Build and run**

`cd ~/repos/flashreader && make && ./build/flashreader`

- [ ] **Step 6: Commit**

`git add -A && git commit -m "chore: project scaffold"`

---

**Note:** Tasks 2-6 only build and test individual modules via the test target. The main binary SOURCES is not updated until Task 7 when everything is wired together. This is intentional — `make` builds the placeholder main.c, `make test` exercises each module.

---

### Task 2: UTF-8 helpers and progress module

Small standalone modules, fully testable.

**Files:**
- Create: `src/utf8.h`, `src/utf8.c`, `src/progress.h`, `src/progress.c`
- Create: `tests/test_main.c`, `tests/test_progress.c`

- [ ] **Step 1: Create utf8.h and utf8.c**

```c
// utf8.h
#ifndef FR_UTF8_H
#define FR_UTF8_H
#include <stddef.h>
/* Count UTF-8 codepoints (not bytes) in a string. */
int utf8_cplen(const char *s);
#endif
```

Implementation: iterate bytes, count only bytes that are NOT continuation bytes (0x80-0xBF).

- [ ] **Step 2: Create progress.h and progress.c**

```c
// progress.h
#ifndef FR_PROGRESS_H
#define FR_PROGRESS_H
#include <stdint.h>
#include <stddef.h>

uint64_t progress_hash(const void *data, size_t len);
int progress_save(uint64_t hash, int word_index);
int progress_load(uint64_t hash);  /* returns word_index or -1 */

#endif
```

Implementation:
- `progress_hash`: 64-bit FNV-1a (FNV offset basis: 0xcbf29ce484222325, FNV prime: 0x100000001b3)
- `progress_save`: create `~/.config/flashreader/` if needed (mkdir -p). Read existing positions.dat, find and update matching hash or append new record. Write back. Each record: 8 bytes hash + 4 bytes word_index. Validate file size is a multiple of 12 — if not, treat as corrupted and ignore (start fresh).
- `progress_load`: read positions.dat, scan for matching hash, return word_index. Return -1 if file is missing, corrupted, or hash not found.

- [ ] **Step 3: Create tests**

`tests/test_main.c`: runner calling `test_progress()`.

`tests/test_progress.c`:
- FNV-1a hash of empty string matches known value (0xcbf29ce484222325)
- FNV-1a hash of "hello" matches known value
- Save then load returns same word_index
- Load non-existent hash returns -1
- Overwrite existing hash updates word_index
- UTF-8 codepoint counting: ASCII, 2-byte, 3-byte, mixed

- [ ] **Step 4: Add test target to Makefile**

```makefile
TEST_SOURCES = tests/test_main.c tests/test_progress.c src/progress.c src/utf8.c
TEST_TARGET = $(BUILDDIR)/test_runner

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SOURCES)
```

- [ ] **Step 5: Run tests, commit**

`make test && git add -A && git commit -m "feat: UTF-8 helpers and progress save/load"`

---

### Task 3: Reader — text loading and word tokenization

**Files:**
- Create: `src/reader.h`, `src/reader.c`
- Create: `tests/test_reader.c`
- Modify: `tests/test_main.c`, `Makefile`

- [ ] **Step 1: Create reader.h**

```c
#ifndef FR_READER_H
#define FR_READER_H

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

/* Load and tokenize a plain text file. chapter_name used for the single chapter. */
int reader_load_file(const char *path, const char *chapter_name, ReaderText *out);

/* Load and tokenize from stdin. */
int reader_load_stdin(ReaderText *out);

/* Load from a raw buffer (used by epub after extraction). */
int reader_load_buffer(const char *text, size_t len, const char *chapter_name, ReaderText *out);

/* Free all memory in a ReaderText. */
void reader_free(ReaderText *rt);

/* Set chapters on an existing ReaderText (used by epub). */
void reader_set_chapters(ReaderText *rt, Chapter *chapters, int count);

#endif
```

- [ ] **Step 2: Create reader.c**

Tokenizer:
- Read file into memory (or use provided buffer)
- Split on whitespace (space, tab, newline, \r). Words preserve attached punctuation.
- Build `words` array (malloc'd array of strdup'd strings)
- Single chapter for plain text (name = filename or "stdin")

- [ ] **Step 3: Create tests**

`tests/test_reader.c`:
- Tokenize "hello world" → 2 words
- Tokenize "hello,  world!\n" → 2 words ("hello," and "world!")
- Tokenize empty string → 0 words
- Tokenize string with only whitespace → 0 words
- Multiple spaces/tabs/newlines between words → correct count
- reader_free doesn't crash on zeroed struct

- [ ] **Step 4: Update Makefile and test runner**

Add `src/reader.c` to test sources. Add `test_reader()` to test_main.c.

- [ ] **Step 5: Run tests, commit**

`make test && git add -A && git commit -m "feat: text reader and word tokenizer"`

---

### Task 4: Inflate (RFC 1951 decompression)

**Files:**
- Create: `src/inflate.h`, `src/inflate.c`
- Create: `tests/test_inflate.c`
- Modify: `tests/test_main.c`, `Makefile`

- [ ] **Step 1: Create inflate.h**

```c
#ifndef FR_INFLATE_H
#define FR_INFLATE_H
#include <stdint.h>
#include <stddef.h>

/* Decompress deflate data (RFC 1951, raw deflate — no zlib/gzip header).
   out_buf must be pre-allocated with sufficient size (caller provides max).
   Returns decompressed size, or -1 on error. */
int inflate_raw(const uint8_t *in, size_t in_len,
                uint8_t *out, size_t out_max);

#endif
```

- [ ] **Step 2: Create inflate.c**

RFC 1951 inflate implementation:
- Bit reader: read bits LSB-first from byte stream
- Block types: 00 (stored), 01 (fixed Huffman), 10 (dynamic Huffman)
- Fixed Huffman: precomputed code tables per spec
- Dynamic Huffman: read code length codes, build literal/length and distance trees
- LZ77 decoding: literal bytes and length/distance back-references
- Length and distance extra bits tables per RFC 1951 section 3.2.5

- [ ] **Step 3: Create tests**

`tests/test_inflate.c`:
- Inflate a stored block (type 00) — manually crafted bytes
- Inflate fixed Huffman data — compress "Hello, World!" and verify decompression
- Inflate dynamic Huffman data — compress a longer string (100+ bytes) to force dynamic codes, verify decompression
- Back-reference test — compress data with repeated patterns, verify LZ77 decoding
- Truncated input returns -1
- Empty input returns -1
- Output buffer too small returns -1

To get test vectors: use Python `zlib.compress` with `wbits=-15` (raw deflate) to generate compressed bytes for known strings. Use longer strings to exercise dynamic Huffman.

- [ ] **Step 4: Update Makefile and test runner**

- [ ] **Step 5: Run tests, commit**

`make test && git add -A && git commit -m "feat: RFC 1951 inflate decompression"`

---

### Task 5: EPUB parser

**Files:**
- Create: `src/epub.h`, `src/epub.c`
- Modify: `src/reader.c` (add EPUB path), `Makefile`

- [ ] **Step 1: Create epub.h**

```c
#ifndef FR_EPUB_H
#define FR_EPUB_H
#include "reader.h"

/* Extract text and chapters from an EPUB file.
   Returns 0 on success, -1 on error. */
int epub_load(const char *path, ReaderText *out);

#endif
```

- [ ] **Step 2: Create epub.c — ZIP reader**

Minimal ZIP reader:
- Seek to end of file, scan backwards for EOCD signature (0x06054b50)
- Parse EOCD: central directory offset and entry count
- Read Central Directory entries: filename, compression method, compressed/uncompressed size, local header offset
- To extract a file: seek to local header offset, skip local header (30 + name_len + extra_len bytes), read data. If method=0 (stored): raw copy. If method=8 (deflated): call `inflate_raw()`.

- [ ] **Step 3: Create epub.c — OPF/content parsing**

1. Extract `META-INF/container.xml`, find `rootfile full-path="..."` attribute → OPF path
2. Extract OPF file. Parse `<manifest>` items (id, href, media-type). Parse `<spine>` itemrefs (idref). Build ordered list of content file paths. Handle namespace prefixes by matching local element names.
3. For each content file in spine order: extract from ZIP, run HTML stripper.

- [ ] **Step 4: Create epub.c — HTML stripper**

State machine:
- States: NORMAL, TAG, SCRIPT, STYLE, COMMENT
- NORMAL: accumulate text characters. On `<`: check for `<!--` (→COMMENT), `<script` (→SCRIPT), `<style` (→STYLE), else →TAG.
- TAG: discard until `>`. On `>` back to NORMAL. If tag was `<h1>`/`<h2>`/`<h3>`: emit chapter marker. If tag was `<br>`, `<p>`, `<div>`: emit space.
- SCRIPT/STYLE: discard until matching `</script>` or `</style>`.
- COMMENT: discard until `-->`.
- Entity decoding: `&amp;` → &, `&lt;` → <, `&gt;` → >, `&quot;` → ", `&nbsp;` → space, `&#NNN;` → UTF-8, `&#xHH;` → UTF-8.

- [ ] **Step 5: Wire into reader**

In `reader_load_file`: if path ends with `.epub`, call `epub_load()` instead of plain text loading.

- [ ] **Step 6: Add HTML stripper and EPUB tests**

Create `tests/test_epub.c` with:
- HTML strip: plain text passes through unchanged
- HTML strip: `<p>hello</p><p>world</p>` → "hello world"
- HTML strip: `<script>var x=1;</script>text` → "text"
- HTML strip: `<style>.a{}</style>text` → "text"
- HTML strip: `<!-- comment -->text` → "text"
- HTML strip: entities `&amp; &lt; &gt; &nbsp; &#65; &#x41;` → "& < >   A A"
- HTML strip: `<h1>Chapter</h1>` detects heading
- HTML strip: nested tags `<p><b>bold</b></p>` → "bold"

Add to test runner and Makefile test sources.

- [ ] **Step 7: Manual test with a real EPUB**

Download a public domain EPUB (e.g., from Project Gutenberg). Run the test target or a quick test program that loads and prints word count + chapter names.

- [ ] **Step 7: Commit**

`git add -A && git commit -m "feat: EPUB parser with ZIP reader and HTML stripper"`

---

### Task 6: Display — terminal rendering and timing loop

**Files:**
- Create: `src/display.h`, `src/display.c`
- Modify: `Makefile`

- [ ] **Step 1: Create display.h**

```c
#ifndef FR_DISPLAY_H
#define FR_DISPLAY_H
#include "reader.h"

typedef struct {
    int wpm;
    int scale_enabled;
    int start_word;
} DisplayOpts;

/* Run the RSVP display loop. Returns final word index on exit. */
int display_run(const ReaderText *text, const DisplayOpts *opts);

/* Call from SIGWINCH handler. */
void display_notify_resize(void);

#endif
```

- [ ] **Step 2: Create display.c — terminal setup**

- `static struct termios orig_termios;`
- `display_init()`: save termios, enter raw mode (cfmakeraw-style), alt screen (`\033[?1049h`), hide cursor (`\033[?25l`). Register `display_shutdown()` via atexit.
- `display_shutdown()`: restore termios, leave alt screen, show cursor.
- Get terminal size: `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)`.

- [ ] **Step 3: Create display.c — rendering**

`render_frame(word, word_idx, word_count, chapters, chapter_count, wpm, paused)`:
1. Clear screen: `\033[2J\033[H`
2. Get current chapter name by finding the last chapter with start_word <= word_idx
3. Center word: move cursor to `(rows/2, (cols - utf8_cplen(word))/2)` using `\033[row;colH`. Print word in bold (`\033[1m`).
4. Progress bar at `rows-1`: filled blocks (`█`), empty blocks (`░`), percentage, WPM. Use colors.
5. Chapter name at `rows`: dim text (`\033[2m`).
6. If paused: show `[PAUSED]` above the word and control hints below.
7. Write all output with `write(STDOUT_FILENO, buf, len)` — build in a buffer first to minimize syscalls.

- [ ] **Step 4: Create display.c — timing and controls**

`display_run()`:
1. Call display_init()
2. Main loop while word_idx < word_count:
   - Compute delay_ms from WPM + word-length scaling + punctuation pause
   - `poll(&(struct pollfd){.fd=STDIN_FILENO, .events=POLLIN}, 1, delay_ms)`. Handle EINTR by recomputing remaining time.
   - If poll returns readable: `read(STDIN_FILENO, &ch, 1)`
     - Space → toggle paused. While paused, poll with -1 timeout (block until keypress).
     - Up → wpm = min(wpm+50, 1000)
     - Down → wpm = max(wpm-50, 50)
     - Right → word_idx = min(word_idx+5, word_count-1)
     - Left → word_idx = max(word_idx-5, 0)
     - Arrow keys: read escape sequence `\033[A/B/C/D`
     - 'q' → break
   - If timeout (word displayed long enough): advance word_idx, render next frame
   - Check resize flag: if set, re-read terminal size, re-render
3. Call display_shutdown()
4. Return word_idx

- [ ] **Step 5: Update Makefile**

Add `src/display.c src/utf8.c` to SOURCES.

- [ ] **Step 6: Commit**

`git add -A && git commit -m "feat: RSVP display with timing, controls, progress bar"`

---

### Task 7: Main — orchestration, arg parsing, integration

**Files:**
- Modify: `src/main.c`
- Modify: `Makefile` (final SOURCES list)

- [ ] **Step 1: Implement full main.c**

```
SOURCES = src/main.c src/reader.c src/epub.c src/inflate.c src/display.c src/progress.c src/utf8.c
```

main.c:
1. Parse args: positional filename (or detect stdin via `!isatty(STDIN_FILENO)`), `--wpm N`, `--no-scale`, `-h`/`--help`
2. Help text:
   ```
   flashreader — terminal speed reader

   Usage: flashreader [options] [file]

   Options:
     --wpm <N>      Words per minute (default: 300, range: 50-1000)
     --no-scale     Disable word-length time scaling
     -h, --help     Show this help

   Controls:
     Space          Pause / resume
     ← →            Skip back / forward 5 words
     ↑ ↓            Adjust WPM ±50
     q              Quit (saves position)

   Supports .txt, .epub, and stdin.
   ```
3. Detect input: `.epub` → epub, stdin pipe → stdin, else → text file
4. Load text via reader
5. If file input (not stdin): hash first 4KB, check progress_load. If found, prompt "Resume from X%? [Y/n]" (read one char, default yes).
6. Set up SIGWINCH handler (sets flag for display)
7. Call `display_run()`
8. On return: if file input, call `progress_save(hash, final_word_idx)`
9. If reached end: print "Done! <word_count> words in <filename>"

- [ ] **Step 2: Build and manual test**

Test with:
- `make && ./build/flashreader some_textfile.txt`
- `echo "This is a test of the speed reader" | ./build/flashreader`
- `./build/flashreader --wpm 500 some_textfile.txt`
- `./build/flashreader --help`
- `./build/flashreader somebook.epub` (if available)

- [ ] **Step 3: Commit**

`git add -A && git commit -m "feat: main orchestration with arg parsing and resume"`

---

### Task 8: README and GitHub repo

**Files:**
- Create: `README.md`

- [ ] **Step 1: Write README.md**

Brief README: one-line description, install (`make && ./build/flashreader book.txt`), usage examples, controls table, how it works, constraints, license.

- [ ] **Step 2: Commit**

`git add -A && git commit -m "docs: README"`

- [ ] **Step 3: Create GitHub repo**

```bash
gh repo create be-lang/flashreader --private --description "Terminal speed reader — one word at a time"
```

- [ ] **Step 4: Squash to single commit and push**

Orphan branch, single "Initial release" commit, push as main.
