#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/progress.h"
#include "../src/utf8.h"

#define TEST(name, cond) do { \
    if (!(cond)) { printf("  FAIL  %s\n", name); fails++; } \
    else { printf("  PASS  %s\n", name); } \
} while(0)

static int test_fnv1a(void)
{
    int fails = 0;
    printf("== FNV-1a hash ==\n");

    /* Empty string should equal the offset basis. */
    TEST("hash empty string",
         progress_hash("", 0) == 0xcbf29ce484222325ULL);

    /* "hello" — known FNV-1a 64-bit value. */
    TEST("hash \"hello\"",
         progress_hash("hello", 5) == 0xa430d84680aabd0bULL);

    return fails;
}

static int test_save_load(void)
{
    int fails = 0;
    printf("== progress save/load ==\n");

    /* Use a temp directory to avoid polluting user config. */
    char tmpdir[] = "/tmp/flashreader_test_XXXXXX";
    if (!mkdtemp(tmpdir)) {
        printf("  FAIL  could not create temp dir\n");
        return 1;
    }

    /* Override HOME so progress functions write to our temp dir. */
    char *old_home = getenv("HOME");
    char *saved_home = old_home ? strdup(old_home) : NULL;
    setenv("HOME", tmpdir, 1);

    uint64_t h1 = 0x1234567890abcdefULL;
    uint64_t h2 = 0xfedcba0987654321ULL;

    /* Load non-existent returns -1. */
    TEST("load missing returns -1",
         progress_load(h1) == -1);

    /* Save then load. */
    TEST("save returns 0",
         progress_save(h1, 42) == 0);
    TEST("load returns saved value",
         progress_load(h1) == 42);

    /* Load different hash returns -1. */
    TEST("load different hash returns -1",
         progress_load(h2) == -1);

    /* Overwrite same hash. */
    TEST("overwrite save returns 0",
         progress_save(h1, 99) == 0);
    TEST("load returns overwritten value",
         progress_load(h1) == 99);

    /* Save second hash, both coexist. */
    TEST("save second hash returns 0",
         progress_save(h2, 7) == 0);
    TEST("load first hash still correct",
         progress_load(h1) == 99);
    TEST("load second hash correct",
         progress_load(h2) == 7);

    /* Restore HOME. */
    if (saved_home) {
        setenv("HOME", saved_home, 1);
        free(saved_home);
    }

    /* Clean up temp files. */
    char cmd[4200];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    (void)system(cmd);

    return fails;
}

static int test_utf8(void)
{
    int fails = 0;
    printf("== UTF-8 cplen ==\n");

    TEST("ASCII \"hello\" == 5",
         utf8_cplen("hello") == 5);
    TEST("empty string == 0",
         utf8_cplen("") == 0);
    TEST("2-byte e-acute == 1",
         utf8_cplen("\xc3\xa9") == 1);
    TEST("3-byte em-dash == 1",
         utf8_cplen("\xe2\x80\x94") == 1);
    TEST("mixed \"cafe\" with accent == 4",
         utf8_cplen("caf\xc3\xa9") == 4);

    return fails;
}

int test_progress(void)
{
    int fails = 0;
    fails += test_fnv1a();
    fails += test_save_load();
    fails += test_utf8();
    return fails;
}
