# flashreader

Terminal speed reader — one word at a time.

RSVP (Rapid Serial Visual Presentation) reader that flashes words in the center of your terminal. Adjustable speed, progress tracking, and position saving so you can pick up where you left off.

## Install

```bash
git clone https://github.com/be-lang/flashreader
cd flashreader
make
```

Requires a C compiler and `make`. Linux / macOS.

## Usage

```bash
flashreader book.txt                    # read a text file
flashreader book.epub                   # read an EPUB
cat article.txt | flashreader           # pipe from stdin
flashreader --wpm 500 book.txt          # start at 500 WPM
flashreader --big book.epub             # large block letters
flashreader --context book.epub         # show surrounding words
flashreader --big --context book.epub   # both
flashreader --scale-speed book.txt      # longer words get more time
```

## Controls

| Key | Action |
|-----|--------|
| Space | Pause / resume |
| ← → | Skip back / forward 5 words |
| ↑ ↓ | Adjust WPM ±50 |
| + / - | Zoom in / out |
| c | Chapter list (EPUB) |
| q | Quit (saves position) |

## How it works

Text is tokenized into words and displayed one at a time with precise timing. For EPUBs, a built-in ZIP reader extracts and decompresses content files (RFC 1951 inflate), then an HTML stripper extracts plain text with chapter detection. Reading position is saved to `~/.config/flashreader/` using a content hash, so it survives file renames.
