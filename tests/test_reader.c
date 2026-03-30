#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/reader.h"

#define TEST(name, cond) do { \
    if (!(cond)) { printf("  FAIL  %s\n", name); fails++; } \
    else { printf("  PASS  %s\n", name); } \
} while(0)

static int test_tokenize_basic(void)
{
    int fails = 0;
    printf("== reader tokenize basic ==\n");

    ReaderText rt;

    /* "hello world" -> 2 words */
    reader_load_buffer("hello world", 11, "test", &rt);
    TEST("hello world count", rt.word_count == 2);
    TEST("hello world [0]", strcmp(rt.words[0], "hello") == 0);
    TEST("hello world [1]", strcmp(rt.words[1], "world") == 0);
    reader_free(&rt);

    /* punctuation preserved */
    reader_load_buffer("hello,  world!\n", 15, "test", &rt);
    TEST("punctuation count", rt.word_count == 2);
    TEST("punctuation [0]", strcmp(rt.words[0], "hello,") == 0);
    TEST("punctuation [1]", strcmp(rt.words[1], "world!") == 0);
    reader_free(&rt);

    /* empty string */
    reader_load_buffer("", 0, "test", &rt);
    TEST("empty count", rt.word_count == 0);
    reader_free(&rt);

    /* whitespace only */
    reader_load_buffer("   \t\n  ", 7, "test", &rt);
    TEST("whitespace-only count", rt.word_count == 0);
    reader_free(&rt);

    /* single word */
    reader_load_buffer("one", 3, "test", &rt);
    TEST("single word count", rt.word_count == 1);
    TEST("single word [0]", strcmp(rt.words[0], "one") == 0);
    reader_free(&rt);

    /* multiple spaces/tabs between words */
    reader_load_buffer("a  \t  b\n\nc", 10, "test", &rt);
    TEST("multi-space count", rt.word_count == 3);
    TEST("multi-space [0]", strcmp(rt.words[0], "a") == 0);
    TEST("multi-space [1]", strcmp(rt.words[1], "b") == 0);
    TEST("multi-space [2]", strcmp(rt.words[2], "c") == 0);
    reader_free(&rt);

    return fails;
}

static int test_chapter(void)
{
    int fails = 0;
    printf("== reader chapter ==\n");

    ReaderText rt;
    reader_load_buffer("hello world", 11, "Chapter 1", &rt);
    TEST("chapter count", rt.chapter_count == 1);
    TEST("chapter name", strcmp(rt.chapters[0].name, "Chapter 1") == 0);
    TEST("chapter start_word", rt.chapters[0].start_word == 0);
    reader_free(&rt);

    return fails;
}

static int test_free_safety(void)
{
    int fails = 0;
    printf("== reader free safety ==\n");

    /* free after load */
    ReaderText rt;
    reader_load_buffer("test", 4, "x", &rt);
    reader_free(&rt);
    TEST("free after load no crash", 1);

    /* free on zeroed struct */
    ReaderText empty;
    memset(&empty, 0, sizeof(empty));
    reader_free(&empty);
    TEST("free zeroed no crash", 1);

    return fails;
}

int test_reader(void)
{
    int fails = 0;
    fails += test_tokenize_basic();
    fails += test_chapter();
    fails += test_free_safety();
    return fails;
}
