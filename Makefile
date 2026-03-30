CC ?= gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O2 -Isrc
BUILDDIR = build

SOURCES = src/main.c src/reader.c src/epub.c src/inflate.c src/display.c src/progress.c src/utf8.c src/bigfont.c
TARGET = $(BUILDDIR)/flashreader

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) -lm

clean:
	rm -rf $(BUILDDIR)

TEST_SOURCES = tests/test_main.c tests/test_progress.c tests/test_reader.c tests/test_inflate.c tests/test_epub.c src/progress.c src/utf8.c src/reader.c src/inflate.c src/epub.c src/display.c src/bigfont.c
TEST_TARGET = $(BUILDDIR)/test_runner

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SOURCES)
