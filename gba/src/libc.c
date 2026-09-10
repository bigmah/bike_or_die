/* The small freestanding C library the GBA port runs on: string and memory
 * routines and a printf good enough for logging and for StrPrintF. */
#include "libc.h"

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = dst; const uint8_t *s = src;
  if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
    uint32_t *dw = (uint32_t *)d; const uint32_t *sw = (const uint32_t *)s;
    while (n >= 16) { dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3]; dw += 4; sw += 4; n -= 16; }
    while (n >= 4) { *dw++ = *sw++; n -= 4; }
    d = (uint8_t *)dw; s = (const uint8_t *)sw;
  } else if ((((uintptr_t)d | (uintptr_t)s) & 1) == 0) {
    uint16_t *dh = (uint16_t *)d; const uint16_t *sh = (const uint16_t *)s;
    while (n >= 2) { *dh++ = *sh++; n -= 2; }
    d = (uint8_t *)dh; s = (const uint8_t *)sh;
  }
  while (n--) *d++ = *s++;
  return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *d = dst; const uint8_t *s = src;
  if (d == s || n == 0) return dst;
  if (d < s || d >= s + n) return memcpy(dst, src, n);
  d += n; s += n;
  if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
    uint32_t *dw = (uint32_t *)d; const uint32_t *sw = (const uint32_t *)s;
    while (n >= 4) { *--dw = *--sw; n -= 4; }
    d = (uint8_t *)dw; s = (const uint8_t *)sw;
  }
  while (n--) *--d = *--s;
  return dst;
}
void *memset(void *dst, int c, size_t n) {
  uint8_t *d = dst;
  if (((uintptr_t)d & 3) == 0 && n >= 4) {
    uint32_t v = (uint8_t)c; v |= v << 8; v |= v << 16;
    uint32_t *dw = (uint32_t *)d;
    while (n >= 16) { dw[0] = v; dw[1] = v; dw[2] = v; dw[3] = v; dw += 4; n -= 16; }
    while (n >= 4) { *dw++ = v; n -= 4; }
    d = (uint8_t *)dw;
  }
  while (n--) *d++ = (uint8_t)c;
  return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
  const uint8_t *p = a, *q = b;
  for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
  return 0;
}
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return p - s; }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) ; return r; }
char *strncpy(char *d, const char *s, size_t n) {
  char *r = d;
  while (n && (*d = *s)) { d++; s++; n--; }
  while (n--) *d++ = 0;
  return r;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n) {
  char *p = d + strlen(d);
  while (n-- && *s) *p++ = *s++;
  *p = 0;
  return d;
}
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (uint8_t)*a - (uint8_t)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
  while (n && *a && *a == *b) { a++; b++; n--; }
  return n ? (uint8_t)*a - (uint8_t)*b : 0;
}
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int abs(int v) { return v < 0 ? -v : v; }
int strcasecmp(const char *a, const char *b) {
  while (*a && tolower((uint8_t)*a) == tolower((uint8_t)*b)) { a++; b++; }
  return tolower((uint8_t)*a) - tolower((uint8_t)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
  while (n && *a && tolower((uint8_t)*a) == tolower((uint8_t)*b)) { a++; b++; n--; }
  return n ? tolower((uint8_t)*a) - tolower((uint8_t)*b) : 0;
}
char *strchr(const char *s, int c) {
  for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; }
}
char *strrchr(const char *s, int c) {
  const char *r = NULL;
  for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}
char *strstr(const char *h, const char *n) {
  size_t l = strlen(n);
  if (!l) return (char *)h;
  for (; *h; h++) if (*h == *n && !strncmp(h, n, l)) return (char *)h;
  return NULL;
}
long strtol(const char *s, char **end, int base) {
  long v = 0; int neg = 0;
  while (isspace((uint8_t)*s)) s++;
  if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
  if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
  if (base == 0) base = 10;
  for (;; s++) {
    int d;
    if (isdigit((uint8_t)*s)) d = *s - '0';
    else if (isalpha((uint8_t)*s)) d = tolower((uint8_t)*s) - 'a' + 10;
    else break;
    if (d >= base) break;
    v = v * base + d;
  }
  if (end) *end = (char *)s;
  return neg ? -v : v;
}
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }

/* ---- printf ---- */
static char *put_num(char *end, uint32_t v, unsigned base, int upper) {
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  *--end = 0;
  do { *--end = digits[v % base]; v /= base; } while (v);
  return end;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
  size_t n = 0;
#define PUT(ch) do { if (n + 1 < size) buf[n] = (ch); n++; } while (0)
  while (*fmt) {
    if (*fmt != '%') { PUT(*fmt++); continue; }
    fmt++;
    int left = 0, zero = 0, plus = 0, width = 0, prec = -1, lng = 0;
    for (;; fmt++) {
      if (*fmt == '-') left = 1; else if (*fmt == '0') zero = 1; else if (*fmt == '+') plus = 1;
      else if (*fmt == ' ' || *fmt == '#') ; else break;
    }
    if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
    else while (isdigit((uint8_t)*fmt)) width = width * 10 + (*fmt++ - '0');
    if (*fmt == '.') {
      fmt++; prec = 0;
      if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
      else while (isdigit((uint8_t)*fmt)) prec = prec * 10 + (*fmt++ - '0');
    }
    while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') { if (*fmt == 'l') lng++; fmt++; }
    char tmp[24], *s = tmp; int len, neg = 0;
    char c = *fmt++;
    switch (c) {
      case 'd': case 'i': {
        int32_t v = va_arg(ap, int32_t);
        uint32_t u = v < 0 ? (neg = 1, (uint32_t)(-(v + 1)) + 1u) : (uint32_t)v;
        s = put_num(tmp + sizeof tmp, u, 10, 0); break;
      }
      case 'u': s = put_num(tmp + sizeof tmp, va_arg(ap, uint32_t), 10, 0); break;
      case 'x': s = put_num(tmp + sizeof tmp, va_arg(ap, uint32_t), 16, 0); break;
      case 'X': s = put_num(tmp + sizeof tmp, va_arg(ap, uint32_t), 16, 1); break;
      case 'p': s = put_num(tmp + sizeof tmp, (uint32_t)(uintptr_t)va_arg(ap, void *), 16, 0); PUT('0'); PUT('x'); zero = 1; if (width < 8) width = 8; break;
      case 'c': tmp[0] = (char)va_arg(ap, int); tmp[1] = 0; s = tmp; break;
      case 's': s = va_arg(ap, char *); if (!s) s = "(null)"; break;
      case '%': PUT('%'); continue;
      case 0: fmt--; continue;
      default: PUT('%'); PUT(c); continue;
    }
    len = (int)strlen(s);
    if (c == 's' && prec >= 0 && len > prec) len = prec;
    int pad = width - len - (neg || (plus && (c == 'd' || c == 'i')));
    if (!left && !zero) while (pad-- > 0) PUT(' ');
    if (neg) PUT('-'); else if (plus && (c == 'd' || c == 'i')) PUT('+');
    if (!left && zero) while (pad-- > 0) PUT('0');
    while (len-- > 0) PUT(*s++);
    if (left) while (pad-- > 0) PUT(' ');
  }
  if (size) buf[n < size ? n : size - 1] = 0;
  return (int)n;
#undef PUT
}
int snprintf(char *buf, size_t size, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, size, fmt, ap); va_end(ap); return r;
}
int sprintf(char *buf, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, 0x7FFFFFFF, fmt, ap); va_end(ap); return r;
}

/* Shell sort: no recursion, no allocation, fine for the few hundred items a level list has. */
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
  uint8_t *b = base, tmp[256];
  size_t gap, i, j;
  if (size > sizeof tmp) return;
  for (gap = n / 2; gap > 0; gap /= 2) {
    for (i = gap; i < n; i++) {
      memcpy(tmp, b + i * size, size);
      for (j = i; j >= gap && cmp(b + (j - gap) * size, tmp) > 0; j -= gap)
        memcpy(b + j * size, b + (j - gap) * size, size);
      memcpy(b + j * size, tmp, size);
    }
  }
}
