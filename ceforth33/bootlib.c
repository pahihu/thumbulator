#include "nolib.h"

#define BUFLEN  (1+32+1)

//------------------------------------------------------------------------
char *itoa ( int n, char *buf, int base )
{
    char tmp[BUFLEN], *p;
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
void nl(void)
{
    puts("");
}
//------------------------------------------------------------------------
void puti(int n, int base)
{
    char buf[BUFLEN];
    puts(itoa(n, buf, base));
}
//------------------------------------------------------------------------
void putd(int n)
{
    puti(n, 10);
}
//------------------------------------------------------------------------
void putx(int n, int ndigits)
{
    char tmp[BUFLEN];
    int i, d;

    i = 0;
    do {
        d = n & 0x0F; n >>= 4;
        tmp[i++] = d < 10 ? d + '0' : d - 10 + 'a';
    } while (--ndigits);

    while (i)
        putc(tmp[--i]);
}
//------------------------------------------------------------------------
// vim:set ts=4 sw=4 et:
