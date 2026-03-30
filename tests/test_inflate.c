#include <stdio.h>
#include <string.h>
#include "../src/inflate.h"

#define TEST(name, cond) do { \
    if (!(cond)) { printf("  FAIL  %s\n", name); fails++; } \
    else { printf("  PASS  %s\n", name); } \
} while(0)

static int test_stored_block(void)
{
    int fails = 0;
    printf("== inflate: stored block ==\n");

    /* Manually crafted type-00 block (BFINAL=1, BTYPE=00):
       Header byte: 0x01  (bfinal=1, btype=00)
       LEN = 5 (0x05 0x00), NLEN = 0xFFFA 0xFF
       Data: "Hello" */
    const uint8_t stored[] = {
        0x01,                         /* BFINAL=1, BTYPE=00 */
        0x05, 0x00,                   /* LEN = 5 */
        0xfa, 0xff,                   /* NLEN = ~5 */
        'H', 'e', 'l', 'l', 'o'
    };
    uint8_t out[64];
    int ret = inflate_raw(stored, sizeof(stored), out, sizeof(out));
    TEST("stored returns 5", ret == 5);
    TEST("stored content", ret == 5 && memcmp(out, "Hello", 5) == 0);

    return fails;
}

static int test_fixed_huffman(void)
{
    int fails = 0;
    printf("== inflate: fixed Huffman ==\n");

    /* "Hello, World!" compressed with raw deflate, fixed Huffman */
    const uint8_t compressed[] = {
        0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0x08,
        0xcf, 0x2f, 0xca, 0x49, 0x51, 0x04, 0x00
    };
    uint8_t out[64];
    int ret = inflate_raw(compressed, sizeof(compressed), out, sizeof(out));
    TEST("fixed huffman returns 13", ret == 13);
    TEST("fixed huffman content",
         ret == 13 && memcmp(out, "Hello, World!", 13) == 0);

    return fails;
}

static int test_lz77_backref(void)
{
    int fails = 0;
    printf("== inflate: LZ77 back-reference ==\n");

    /* "A" * 200, raw deflate */
    const uint8_t compressed[] = {
        0x73, 0x74, 0x1c, 0x1e, 0x00, 0x00
    };
    uint8_t out[256];
    int ret = inflate_raw(compressed, sizeof(compressed), out, sizeof(out));
    TEST("lz77 returns 200", ret == 200);

    int all_a = 1;
    if (ret == 200) {
        for (int i = 0; i < 200; i++) {
            if (out[i] != 'A') { all_a = 0; break; }
        }
    } else {
        all_a = 0;
    }
    TEST("lz77 all A's", all_a);

    return fails;
}

static int test_dynamic_huffman(void)
{
    int fails = 0;
    printf("== inflate: dynamic Huffman ==\n");

    /* "The quick brown fox jumps over the lazy dog. " * 5 */
    const uint8_t compressed[] = {
        0x0b, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c,
        0xce, 0x56, 0x48, 0x2a, 0xca, 0x2f, 0xcf, 0x53,
        0x48, 0xcb, 0xaf, 0x50, 0xc8, 0x2a, 0xcd, 0x2d,
        0x28, 0x56, 0xc8, 0x2f, 0x4b, 0x2d, 0x52, 0x28,
        0x01, 0x4a, 0xe7, 0x24, 0x56, 0x55, 0x2a, 0xa4,
        0xe4, 0xa7, 0xeb, 0x29, 0x84, 0x0c, 0x41, 0xc5,
        0x00
    };

    const char *expected =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. ";
    size_t elen = strlen(expected);

    uint8_t out[512];
    int ret = inflate_raw(compressed, sizeof(compressed), out, sizeof(out));
    TEST("dynamic returns correct length", ret == (int)elen);
    TEST("dynamic content",
         ret == (int)elen && memcmp(out, expected, elen) == 0);

    return fails;
}

static int test_truncated_input(void)
{
    int fails = 0;
    printf("== inflate: error handling ==\n");

    /* Only 2 bytes of valid compressed data — should fail */
    const uint8_t truncated[] = { 0xf3, 0x48 };
    uint8_t out[64];
    int ret = inflate_raw(truncated, sizeof(truncated), out, sizeof(out));
    TEST("truncated input returns -1", ret == -1);

    return fails;
}

static int test_output_too_small(void)
{
    int fails = 0;
    printf("== inflate: output buffer too small ==\n");

    const uint8_t compressed[] = {
        0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0x08,
        0xcf, 0x2f, 0xca, 0x49, 0x51, 0x04, 0x00
    };
    uint8_t out[1];
    int ret = inflate_raw(compressed, sizeof(compressed), out, 1);
    TEST("output too small returns -1", ret == -1);

    return fails;
}

int test_inflate(void)
{
    int fails = 0;
    fails += test_stored_block();
    fails += test_fixed_huffman();
    fails += test_lz77_backref();
    fails += test_dynamic_huffman();
    fails += test_truncated_input();
    fails += test_output_too_small();
    return fails;
}
