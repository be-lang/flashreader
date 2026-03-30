#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/epub.h"

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } \
} while (0)

static void test_plain_text(void)
{
    const char *html = "hello world";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "plain text returns non-NULL");
    ASSERT(strcmp(result, "hello world") == 0, "plain text passes through");
    free(result);
}

static void test_tags_stripped(void)
{
    const char *html = "<p>hello</p><p>world</p>";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "tags stripped returns non-NULL");
    ASSERT(strstr(result, "hello") != NULL, "tags stripped: hello present");
    ASSERT(strstr(result, "world") != NULL, "tags stripped: world present");
    ASSERT(strchr(result, '<') == NULL, "tags stripped: no angle brackets");
    free(result);
}

static void test_script_removed(void)
{
    const char *html = "<script>var x=1;</script>text";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "script removed returns non-NULL");
    ASSERT(strstr(result, "var") == NULL, "script content removed");
    ASSERT(strstr(result, "text") != NULL, "text after script preserved");
    free(result);
}

static void test_style_removed(void)
{
    const char *html = "<style>.a{}</style>text";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "style removed returns non-NULL");
    ASSERT(strstr(result, ".a") == NULL, "style content removed");
    ASSERT(strstr(result, "text") != NULL, "text after style preserved");
    free(result);
}

static void test_comment_removed(void)
{
    const char *html = "<!-- comment -->text";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "comment removed returns non-NULL");
    ASSERT(strstr(result, "comment") == NULL, "comment content removed");
    ASSERT(strstr(result, "text") != NULL, "text after comment preserved");
    free(result);
}

static void test_entities(void)
{
    const char *html = "&amp; &lt; &gt;";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "entities returns non-NULL");
    ASSERT(strstr(result, "& < >") != NULL, "entities decoded");
    free(result);
}

static void test_numeric_entity(void)
{
    const char *html = "&#65;";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "numeric entity returns non-NULL");
    ASSERT(strcmp(result, "A") == 0, "&#65; -> A");
    free(result);
}

static void test_hex_entity(void)
{
    const char *html = "&#x41;";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "hex entity returns non-NULL");
    ASSERT(strcmp(result, "A") == 0, "&#x41; -> A");
    free(result);
}

static void test_nested_tags(void)
{
    const char *html = "<p><b>bold</b></p>";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "nested tags returns non-NULL");
    ASSERT(strstr(result, "bold") != NULL, "nested tags: bold present");
    ASSERT(strchr(result, '<') == NULL, "nested tags: no angle brackets");
    free(result);
}

static void test_heading_detection(void)
{
    const char *html = "<h1>Chapter One</h1>some text here";
    Chapter *chapters = NULL;
    int ch_count = 0;
    char *result = html_strip(html, strlen(html), &chapters, &ch_count, 0);
    ASSERT(result != NULL, "heading detection returns non-NULL");
    ASSERT(ch_count == 1, "heading detection: one chapter found");
    if (ch_count > 0) {
        ASSERT(strcmp(chapters[0].name, "Chapter One") == 0,
               "heading detection: chapter name correct");
        ASSERT(chapters[0].start_word == 0,
               "heading detection: start_word is 0");
    }
    free(chapters);
    free(result);
}

static void test_heading_with_offset(void)
{
    const char *html = "some text <h2>Part Two</h2>more words";
    Chapter *chapters = NULL;
    int ch_count = 0;
    char *result = html_strip(html, strlen(html), &chapters, &ch_count, 100);
    ASSERT(result != NULL, "heading offset returns non-NULL");
    ASSERT(ch_count == 1, "heading offset: one chapter found");
    if (ch_count > 0) {
        ASSERT(strcmp(chapters[0].name, "Part Two") == 0,
               "heading offset: chapter name correct");
        ASSERT(chapters[0].start_word == 102,
               "heading offset: start_word includes offset");
    }
    free(chapters);
    free(result);
}

static void test_multibyte_entity(void)
{
    /* U+00E9 is e-acute, 2-byte UTF-8: 0xC3 0xA9 */
    const char *html = "&#233;";
    char *result = html_strip(html, strlen(html), NULL, NULL, 0);
    ASSERT(result != NULL, "multibyte entity returns non-NULL");
    ASSERT((unsigned char)result[0] == 0xC3 &&
           (unsigned char)result[1] == 0xA9,
           "&#233; -> UTF-8 e-acute");
    free(result);
}

int test_epub(void)
{
    printf("test_epub:\n");
    test_plain_text();
    test_tags_stripped();
    test_script_removed();
    test_style_removed();
    test_comment_removed();
    test_entities();
    test_numeric_entity();
    test_hex_entity();
    test_nested_tags();
    test_heading_detection();
    test_heading_with_offset();
    test_multibyte_entity();
    printf("  %d/%d passed\n", tests_run - tests_failed, tests_run);
    return tests_failed;
}
