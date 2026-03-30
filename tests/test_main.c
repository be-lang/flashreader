#include <stdio.h>

extern int test_progress(void);
extern int test_reader(void);
extern int test_inflate(void);
extern int test_epub(void);

int main(void)
{
    int fail = 0;
    fail += test_progress();
    fail += test_reader();
    fail += test_inflate();
    fail += test_epub();
    printf("\n%s\n", fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fail ? 1 : 0;
}
