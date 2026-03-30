#include "utf8.h"

int utf8_cplen(const char *s)
{
    int count = 0;
    for (; *s; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80)
            count++;
    }
    return count;
}
