#include "nolib.h"

#define THUL_UART_BASE 0xE0000000
#define THUL_UART_TX  ((uint32_t*)(THUL_UART_BASE + 0x00))
#define THUL_UART_RX  (( int32_t*)(THUL_UART_BASE + 0x04))
#define THUL_UART_QRX (( int32_t*)(THUL_UART_BASE + 0x08))
//------------------------------------------------------------------------
void uart_tx ( uint32_t c )
{
    THUL_UART_TX[0] = c;
}
int uart_rx ( void )
{
    return THUL_UART_RX[0];
}
//------------------------------------------------------------------------
int uart_qrx (void)
{
    return THUL_UART_QRX[0];
}
//------------------------------------------------------------------------
void putc(int c)
{
    uart_tx(c);
}
//------------------------------------------------------------------------
int getc(void)
{
    int c;

    c = uart_qrx();
    return c ? uart_rx() : c;
}
//------------------------------------------------------------------------
size_t strlen(const char *s)
{
    size_t n = 0;

    while (*s++) n++;
    return n;
}
//------------------------------------------------------------------------
void *memset ( void *b, int c, size_t size )
{
    unsigned char *p;

    p = b;
    while (size--)
        *p++ = c;

    return b;
}
//------------------------------------------------------------------------
void *memcpy ( void *dst, const void *src, size_t size )
{
    unsigned char *p;
    const unsigned char *q;

    p = dst; q = src;
    while (size--)
        *p++ = *q++;

    return dst;
}
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
int puts(const char *s)
{
    char c;

    while ((c = *s++))
        uart_tx (c);

    return 0;
}
//------------------------------------------------------------------------
void hexstring ( uint32_t d )
{
    uint32_t rb;
    uint32_t rc;

    rb=32;
    while(1)
    {
        rb-=4;
        rc=(d>>rb)&0xF;
        if(rc>9) rc+=0x37; else rc+=0x30;
        uart_tx(rc);
        if(rb==0) break;
    }
    uart_tx(0x0D);
    uart_tx(0x0A);
}
//------------------------------------------------------------------------
uint32_t prand32 ( uint32_t x )
{
    if(x&1)
    {
        x=x>>1;
        x=x^0xBF9EC099;
    }
    else
    {
        x=x>>1;
    }
    return(x);
}
//------------------------------------------------------------------------
void nl(void)
{
    puts("\n");
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
//------------------------------------------------------------------------
void putx(int n, int ndigits)
{
    char tmp[33];
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
void exit(int status)
{
    asm("swi 1");
}
//------------------------------------------------------------------------
// vim:set ts=4 sw=4 et:
