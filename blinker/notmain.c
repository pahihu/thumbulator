
//------------------------------------------------------------------------
//------------------------------------------------------------------------
void PUT32 ( unsigned int, unsigned int);
#define THUL_UART_BASE 0xE0000000
//------------------------------------------------------------------------
void uart_putc ( unsigned int c )
{
    PUT32(THUL_UART_BASE+0x0,c);
}
//------------------------------------------------------------------------
void *memset ( void *b, int c, unsigned int size )
{
    unsigned char *p;

    p = b;
    while (size--)
        *p++ = c;

    return b;
}
//------------------------------------------------------------------------
void *memcpy ( void *dst, const void *src, unsigned int size )
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
        tmp[i++] = d < 10 ? d + '0' : d - 10 + 'a';
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
        uart_putc (c);

    return 0;
}
//------------------------------------------------------------------------
void hexstring ( unsigned int d )
{
    //unsigned int ra;
    unsigned int rb;
    unsigned int rc;

    rb=32;
    while(1)
    {
        rb-=4;
        rc=(d>>rb)&0xF;
        if(rc>9) rc+=0x37; else rc+=0x30;
        uart_putc(rc);
        if(rb==0) break;
    }
    uart_putc(0x0D);
    uart_putc(0x0A);
}
//------------------------------------------------------------------------
unsigned int prand32 ( unsigned int x )
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
void puti(int n)
{
    char buf[32+1];
    puts(itoa(n, buf, 10));
}
//------------------------------------------------------------------------
void fortytwo(int base)
{
    char buf[32+1];

    puts("42 in base ");
    puti(base);
    puts(" is ");
    puts(itoa(42, buf, base));
    nl();
}
//------------------------------------------------------------------------
int fib(int n)
{
    if (n < 2)
        return n;
    return fib(n-1) + fib(n-2);
}
//------------------------------------------------------------------------
int notmain ( void )
{
    volatile unsigned int ra;
    unsigned int rb;
    unsigned int prand;

    for(rb=0;rb<5;rb++)
    {
        uart_putc((rb&7)|0x30);
        for(ra=0;ra<0x321;ra++) continue ;
    }

    prand=0x12345678;
    for(rb=0;rb<5;rb++)
    {
        prand=prand32(prand);
        hexstring(prand);
    }

    puts("Hello, world! ");
    fortytwo(10);
    fortytwo( 2);
    fortytwo( 8);
    fortytwo(16);
    fortytwo(36);

    puts("Fib(30) = "); puti(fib(30)); nl();

    return(0);
}
//------------------------------------------------------------------------
//------------------------------------------------------------------------

