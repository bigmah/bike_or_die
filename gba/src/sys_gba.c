/* libpit's sys_* and xalloc services on the GBA: no files, no threads, no
 * floating point worth the name. */
#include <stdint.h>
#include "sys.h"
#include "xalloc.h"
#include "libc.h"
#include "heap.h"
#include "gba.h"

int64_t sys_get_clock(void);   /* microseconds, in timer.c */
uint64_t sys_time(void) { return 3900000000u + (uint64_t)(sys_get_clock() / 1000000); }
void sys_usleep(uint32_t us) { int64_t t = sys_get_clock() + us; while (sys_get_clock() < t) ; }
char *sys_getenv(char *name) { (void)name; return NULL; }
int sys_country(char *country, int len) { if (len > 2) strcpy(country, "US"); return 0; }
int sys_language(char *language, int len) { if (len > 2) strcpy(language, "en"); return 0; }
int sys_errno(void) { return 0; }
void sys_exit(int r) { (void)r; for (;;) ; }

void *sys_malloc(sys_size_t size) { return heap_alloc(size); }
void sys_free(void *ptr) { heap_free(ptr); }
void *sys_calloc(sys_size_t nmemb, sys_size_t size) { return heap_alloc(nmemb * size); }
void *sys_realloc(void *ptr, sys_size_t size) { return heap_realloc(ptr, size); }
char *sys_strdup(const char *s) { char *d = heap_alloc(strlen(s) + 1); if (d) strcpy(d, s); return d; }
char *sys_strncpy(char *dest, const char *src, sys_size_t n) { return strncpy(dest, src, n); }
char *sys_strcpy(char *dest, const char *src) { return strcpy(dest, src); }
sys_size_t sys_strlen(const char *s) { return strlen(s); }
sys_size_t sys_strnlen(const char *s, sys_size_t n) { sys_size_t i = 0; while (i < n && s[i]) i++; return i; }
char *sys_strchr(const char *s, int c) { return strchr(s, c); }
char *sys_strrchr(const char *s, int c) { return strrchr(s, c); }
char *sys_strstr(const char *h, const char *n) { return strstr(h, n); }
int sys_strcmp(const char *a, const char *b) { return strcmp(a, b); }
int sys_strncmp(const char *a, const char *b, sys_size_t n) { return strncmp(a, b, n); }
int sys_strcasecmp(const char *a, const char *b) { return strcasecmp(a, b); }
int sys_strncasecmp(const char *a, const char *b, sys_size_t n) { return strncasecmp(a, b, n); }
char *sys_strcat(char *d, const char *s) { return strcat(d, s); }
char *sys_strncat(char *d, const char *s, sys_size_t n) { return strncat(d, s, n); }
int sys_atoi(const char *s) { return atoi(s); }
long sys_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
unsigned long sys_strtoul(const char *s, char **e, int b) { return (unsigned long)strtol(s, e, b); }
double sys_atof(const char *s) { return (double)atoi(s); }
int sys_memcmp(const void *a, const void *b, sys_size_t n) { return memcmp(a, b, n); }
void *sys_memmove(void *d, const void *s, sys_size_t n) { return memmove(d, s, n); }
void *sys_memcpy(void *d, const void *s, sys_size_t n) { return memcpy(d, s, n); }
void *sys_memset(void *d, int c, sys_size_t n) { return memset(d, c, n); }
void *sys_memchr(const void *s, int c, sys_size_t n) { const uint8_t *p = s; while (n--) { if (*p == (uint8_t)c) return (void *)p; p++; } return NULL; }
int sys_abs(int x) { return x < 0 ? -x : x; }
int sys_toupper(int c) { return toupper(c); }
int sys_tolower(int c) { return tolower(c); }
int sys_isspace(int c) { return isspace(c); }
int sys_isdigit(int c) { return isdigit(c); }
int sys_isalpha(int c) { return isalpha(c); }
int sys_isalnum(int c) { return isalnum(c); }
int sys_isupper(int c) { return isupper(c); }
int sys_islower(int c) { return islower(c); }
int sys_isprint(int c) { return c >= 32 && c < 127; }
int sys_isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int sys_iscntrl(int c) { return c < 32 || c == 127; }
int sys_ispunct(int c) { return sys_isprint(c) && !isalnum(c) && c != ' '; }
int sys_isblank(int c) { return c == ' ' || c == '\t'; }
int sys_isgraph(int c) { return sys_isprint(c) && c != ' '; }
int sys_sprintf(char *str, const char *format, ...) { va_list ap; int r; va_start(ap, format); r = vsnprintf(str, 0x7FFFFFFF, format, ap); va_end(ap); return r; }
int sys_snprintf(char *str, sys_size_t size, const char *format, ...) { va_list ap; int r; va_start(ap, format); r = vsnprintf(str, size, format, ap); va_end(ap); return r; }
int sys_vsprintf(char *str, const char *format, sys_va_list ap) { return vsnprintf(str, 0x7FFFFFFF, format, ap); }
int sys_vsnprintf(char *str, sys_size_t size, const char *format, sys_va_list ap) { return vsnprintf(str, size, format, ap); }
int sys_sscanf(const char *str, const char *format, ...) { (void)str; (void)format; return 0; }
void sys_qsort(void *base, sys_size_t nmemb, sys_size_t size, int (*compar)(const void *, const void *)) { qsort(base, nmemb, size, compar); }
static unsigned int seed = 12345;
int sys_rand(void) { seed = seed * 1103515245u + 12345u; return (int)((seed >> 16) & 0x7FFF); }
void sys_srand(unsigned int s) { seed = s; }

/* The little floating point that Bitmap.c and Form.c ask for. */
double sys_floor(double x) { double r = (double)(int32_t)x; return (r > x) ? r - 1.0 : r; }
double sys_ceil(double x) { double r = (double)(int32_t)x; return (r < x) ? r + 1.0 : r; }
double sys_fabs(double x) { return x < 0 ? -x : x; }
double sys_pi(void) { return 3.14159265358979323846; }
double sys_sqrt(double x) { double r = x > 1 ? x : 1; int i; if (x <= 0) return 0; for (i = 0; i < 24; i++) r = 0.5 * (r + x / r); return r; }
double sys_sin(double x) {
  double x2, r; int i; double term;
  while (x > 3.14159265358979) x -= 6.28318530717959;
  while (x < -3.14159265358979) x += 6.28318530717959;
  x2 = x * x; term = x; r = x;
  for (i = 1; i < 8; i++) { term *= -x2 / ((2 * i) * (2 * i + 1)); r += term; }
  return r;
}
double sys_cos(double x) { return sys_sin(x + 1.5707963267949); }
double sys_pow(double x, double y) { double r = 1; int n = (int)y; while (n-- > 0) r *= x; return r; }
double sys_atan2(double y, double x) { (void)y; (void)x; return 0; }
int sys_isnan(double x) { return x != x; }
int sys_isinf(double x) { (void)x; return 0; }

/* xalloc: the debug variants are what the macros expand to */
void *xmalloc_debug(const char *file, const char *func, int line, sys_size_t size) { (void)file; (void)func; (void)line; return heap_alloc(size); }
void xfree_debug(const char *file, const char *func, int line, void *ptr) { (void)file; (void)func; (void)line; heap_free(ptr); }
void *xcalloc_debug(const char *file, const char *func, int line, sys_size_t nmemb, sys_size_t size) { (void)file; (void)func; (void)line; return heap_alloc(nmemb * size); }
void *xrealloc_debug(const char *file, const char *func, int line, void *ptr, sys_size_t size) { (void)file; (void)func; (void)line; return heap_realloc(ptr, size); }
char *xstrdup_debug(const char *file, const char *func, int line, const char *s) { (void)file; (void)func; (void)line; return sys_strdup(s); }
void *xmemcpy_debug(const char *file, const char *func, int line, void *dest, const void *src, sys_size_t n) { (void)file; (void)func; (void)line; return memcpy(dest, src, n); }
void *xmemset_debug(const char *file, const char *func, int line, void *s, int c, sys_size_t n) { (void)file; (void)func; (void)line; return memset(s, c, n); }
