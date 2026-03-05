#include "util.h"

char numbuf[6];

int int2digits(int16_t n)
{
    int first = 0;
    int len = 0;
    for (int i = sizeof(numbuf); --i >= 0;) {
        int rem = n % 10;
        numbuf[i] = rem;
        n /= 10;
        ++len;
        if (first++ == 0) continue;
        if (n == 0 && rem == 0) {
            numbuf[i] = 10; // blank
            --len;
        }
    }
    return len;
}

