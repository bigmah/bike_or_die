/* The small freestanding C library the GBA port runs on. */
#ifndef BOD_LIBC_H
#define BOD_LIBC_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
int   strcasecmp(const char *a, const char *b);
int   strncasecmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
int   atoi(const char *s);
long  strtol(const char *s, char **end, int base);
int   toupper(int c);
int   tolower(int c);
int   isdigit(int c);
int   isspace(int c);
int   isalpha(int c);
int   isalnum(int c);
int   isupper(int c);
int   islower(int c);
int   abs(int v);
int   vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int   snprintf(char *buf, size_t size, const char *fmt, ...);
int   sprintf(char *buf, const char *fmt, ...);
void  qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
#endif
