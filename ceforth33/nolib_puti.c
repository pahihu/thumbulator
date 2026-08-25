#include "nolib.h"
// NOTE: if clang compiles as part of nolib.c, when puti/putd/itoa not used
//       --gc-sections removes them, but the __aeabi_idiv remains.
//------------------------------------------------------------------------
char *itoa ( int n, char *buf, int base )
{
    char tmp[33], *p;
    int sign, i, d;

    sign = 0;
    if (n < 0) {
        sign = 1; n = -n;
    }
    i = 0;
    do {
        d = n % base; n /= base;
        tmp[i++] = d < 10 ? d + '0' : d - 10 + 'A';
    } while (n);

    p = buf;
    if (sign)
        *p++ = '-';
    while (i)
        *p++ = tmp[--i];
    *p = '\0';

    return buf; 
}
//------------------------------------------------------------------------
void puti(int n, int base)
{
    char buf[32+1];
    puts(itoa(n, buf, base));
}
//------------------------------------------------------------------------
void putd(int n)
{
    puti(n, 10);
}
