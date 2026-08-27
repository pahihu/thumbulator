#include "nolib.h"

#define THUL_UART_BASE 0xE0000000
#define THUL_UART_TX  ((volatile uint32_t*)(THUL_UART_BASE + 0x00))
#define THUL_UART_RX  ((volatile  int32_t*)(THUL_UART_BASE + 0x04))
#define THUL_UART_QRX ((volatile  int32_t*)(THUL_UART_BASE + 0x08))
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

#if 1
    while (!(c = uart_qrx()));
    c = uart_rx();
    return c;
#else
    c = uart_qrx();
    return c ? uart_rx() : c;
#endif

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
#ifdef USE_FOR_LOOPS
    size_t i;
    for (i = 0; i < size; i++) *p++ = c;
#else
    while (size--)
        *p++ = c;
#endif

    return b;
}
//------------------------------------------------------------------------
void *memcpy ( void *dst, const void *src, size_t size )
{
    unsigned char *p;
    const unsigned char *q;
    size_t i;

    p = dst; q = src;
#ifdef USE_FOR_LOOPS
    for (i = 0; i < size; i++) *p++ = *q++;
#else
    while (size--)
        *p++ = *q++;
#endif

    return dst;
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
    putc(10);
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
    puts("HALTED");
    // asm("swi 1");
    asm("b .");
}
//------------------------------------------------------------------------
static int __nolib_errno;
int *__errno()
{
    return &__nolib_errno;
}
extern int _data_end;
static void *__data_seg = 0;
void *sbrk(int increment)
{
    void *ret;

    if (0 == __data_seg)
    {
        __data_seg = (void*)&_data_end;
        puts("__data_seg="); putx((int)__data_seg,8); nl();
    }

    ret = __data_seg;
    __data_seg += increment;
    return ret;
}
//------------------------------------------------------------------------
void *malloc(size_t size)
{
    return sbrk(size);
}
// vim:set ts=4 sw=4 et:
