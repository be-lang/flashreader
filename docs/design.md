# flashreader — terminal speed reading tool

## Overview

RSVP (Rapid Serial Visual Presentation) speed reader for the terminal. Displays one word at a time, centered, with adjustable speed. Supports plain text, EPUB, and stdin. Saves position for resuming later. Zero dependencies, pure C.

## Usage

```
flashreader book.txt                  # read a text file
flashreader book.epub                 # read an EPUB
cat article.txt | flashreader         # pipe from stdin
flashreader book.txt --wpm 500        # start at 500 WPM
flashreader book.txt --no-scale       # disable word-length time scaling
```

## Interface

```
┌─────────────────────────────────────────┐
│                                         │
│                                         │
│                                         │
│              nevertheless               │
│                                         │
│                                         │
│                                         │
│  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░  62%  350wpm │
│  Chapter 3: The Garden                  │
└─────────────────────────────────────────┘
```

- Single word prominently centered in the terminal with surrounding whitespace
- Progress bar at bottom: filled/unfilled blocks, percentage, current WPM
- Chapter name shown below progress bar (EPUB only; for text files, shows filename)
- Paused state shows `[PAUSED]` indicator and control hints

## Controls

- **Space**: pause / resume
- **Left arrow**: skip back 5 words (clamped to 0)
- **Right arrow**: skip forward 5 words (clamped to end)
- **Up arrow**: increase WPM by 50 (max 1000)
- **Down arrow**: decrease WPM by 50 (min 50)
- **q**: quit (auto-saves position)

## Word timing

Base delay = `60.0 / wpm` seconds per word.

When word-length scaling is enabled (default):
- Words <= 4 chars: base delay (char count = UTF-8 codepoints, not bytes)
- Words 5-8 chars: base delay × 1.2
- Words 9-12 chars: base delay × 1.4
- Words > 12 chars: base delay × 1.6

Punctuation pause: words ending in `.` `!` `?` `:` `;` get an extra 50% delay (sentence boundary feels natural).

Disable scaling with `--no-scale` for uniform timing.

## Save / Resume

Position is saved to `~/.config/flashreader/positions.dat`.

Key: 64-bit FNV-1a hash of the first 4KB of the file content (handles renames, avoids storing full paths).
Value: word index (4 bytes).

File format: simple binary, array of {hash (8 bytes), word_index (4 bytes)} records.

On open: if a saved position exists for this file, prompt "Resume from 62%? [Y/n]".
On quit: auto-save current word index.
Stdin input does not save position (no stable identifier).

## UTF-8 handling

All text is assumed UTF-8. Word centering uses display width (codepoint count) not byte count. A helper function counts UTF-8 codepoints by skipping continuation bytes (0x80-0xBF). This is sufficient for Latin, Cyrillic, Greek, and most scripts where one codepoint = one column. CJK wide characters are out of scope for v1.

## Architecture

### main.c — Entry point

- Parse args: filename (or detect stdin), `--wpm N`, `--no-scale`, `-h`/`--help`
- Detect input type: `.epub` extension → EPUB, stdin → pipe, otherwise → plain text
- Load text content via reader module
- Check for saved position, prompt to resume (skip prompt for stdin)
- Enter display loop
- On exit: save position (skip for stdin)

### reader.c — Text loading and word tokenization

- `reader_load_file(path)` → returns a `ReaderText` struct with:
  - `char **words` — array of word strings
  - `int word_count`
  - `Chapter *chapters` — array of {name, start_word_index} (EPUB only)
  - `int chapter_count`
- `reader_load_stdin()` → same but reads all of stdin into memory first
- For plain text: split on whitespace, single chapter named after filename
- For EPUB: delegate to epub module, then tokenize the extracted text
- Words preserve attached punctuation (e.g. "hello," is one word)

### epub.c — EPUB parser

EPUB files are ZIP archives containing XHTML content files.

- `epub_extract(path, text_out, chapters_out)`:
  1. Open ZIP: read End of Central Directory record (scan backwards up to 65557 bytes for EOCD signature, validate comment length field). Find Central Directory, iterate file entries.
  2. Find `META-INF/container.xml` → extract path to `.opf` file (root file)
  3. Parse `.opf` to get the ordered list of content files (spine → manifest items). Handle XML namespace prefixes (match on local name, ignore prefix).
  4. For each content file (XHTML): decompress if deflated, strip HTML tags, extract text
  5. Detect chapter boundaries from `<h1>`/`<h2>`/`<h3>` tags before stripping
  6. Concatenate all text in spine order

ZIP reading: minimal implementation supporting stored (method 0) and deflated (method 8) entries. Only needs to read, not write.

Deflate decompression: self-contained inflate implementation supporting both fixed and dynamic Huffman codes (RFC 1951). This is ~500-700 lines but well-understood — implement based on the spec directly.

HTML stripping: state machine with states for: normal text, inside tag, inside `<script>`, inside `<style>`, inside comment (`<!-- -->`). Decode common entities (`&amp;` `&lt;` `&gt;` `&quot;` `&nbsp;` and numeric entities `&#NNN;` / `&#xHH;`). Convert block elements (`<br>`, `<p>`, `<div>`) to whitespace.

### display.c — Terminal rendering and timing loop

- `display_init()`: enter raw mode (termios), alt screen, hide cursor, register atexit for restore
- `display_shutdown()`: restore terminal
- `display_run(words, word_count, chapters, chapter_count, wpm, scale_enabled)`:
  - Main loop using `poll()` on STDIN with timeout = word delay. Handle EINTR from signals by re-polling for remaining time.
  - Render current word centered in terminal
  - Render progress bar + stats at bottom
  - Handle keypresses: space (pause), arrows (skip/wpm), q (quit)
  - Returns final word index (for save)
- SIGWINCH: signal handler sets a flag (`volatile sig_atomic_t`), main loop checks flag and re-reads terminal size via `ioctl(TIOCGWINSZ)`.

### progress.c — Position save/load

- `progress_save(file_hash, word_index)`: write to `~/.config/flashreader/positions.dat`
- `progress_load(file_hash)`: returns saved word index, or -1 if not found
- `progress_hash(data, len)`: 64-bit FNV-1a hash

## Error handling

- Malformed/truncated EPUB: print error message and exit (don't crash)
- Missing OPF or content files: skip missing files, continue with what's available
- Corrupted positions.dat: ignore saved position, start from beginning
- Terminal too narrow: render what fits, truncate progress bar

## Build

```
make && ./build/flashreader book.txt
```

No root required. Works on any terminal.

## Constraints

- Linux / macOS (POSIX terminal, termios)
- Zero external dependencies
- Single-threaded
- C11
- EPUB support: stored and deflated ZIP entries only (covers 99% of EPUBs)
- No DRM — cannot read DRM-protected EPUBs
- UTF-8 only (no UTF-16 or other encodings)
