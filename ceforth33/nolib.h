#ifndef _NOLIB_H
#define _NOLIB_H

typedef int int32_t;
typedef long long int64_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef unsigned int size_t;

void *memset ( void *b, int c, size_t size );
void *memcpy ( void *dst, const void *src, size_t size );

char *itoa ( int n, char *buf, int base );

void putc(int c);
int  puts(const char *s);
void puti(int n, int base);
void putx(int n, int digits);
void putd(int n);
void nl(void);

int    getc(void);
size_t strlen(const char *s);
void   exit(int);

void hexstring ( uint32_t d );
uint32_t prand32 ( uint32_t x );

#endif
