/* PumpkinOS's debug() goes to mGBA's log. Levels: 0 error, 1 info, 2 trace.
 * Trace is off unless a subsystem is named in debug_setsyslevel. */
#include <stdint.h>
#include "sys.h"
#include "debug.h"
#include "libc.h"
#include "gba.h"

static int global_level = DEBUG_INFO;
static char trace_sys[4][16];
static int ntrace;

int debug_init(char *filename) { (void)filename; return 0; }
int debug_close(void) { return 0; }
void debug_scope(int show) { (void)show; }
void debug_aindent(int i) { (void)i; }
void debug_indent(int incr) { (void)incr; }
void debug_rawtty(int raw) { (void)raw; }

void debug_setsyslevel(char *sys, int level) {
  if (level >= DEBUG_TRACE) bod_trace_any = 1;
  if (!sys) { global_level = level; return; }
  if (ntrace < 4) { strncpy(trace_sys[ntrace], sys, 15); ntrace++; }
}

int debug_getsyslevel(char *sys) {
  int i;
  for (i = 0; i < ntrace; i++) if (!strcmp(trace_sys[i], sys)) return DEBUG_TRACE;
  return global_level;
}

int bod_trace_any;
static int wanted(int level, const char *sys) {
  int i;
  if (level <= global_level) return 1;
  for (i = 0; i < ntrace; i++) if (!strcmp(trace_sys[i], sys)) return 1;
  return 0;
}

void debugva_full(const char *file, const char *func, int line, int level, const char *sys, const char *fmt, sys_va_list ap) {
  char buf[200];
  (void)file; (void)func; (void)line;
  if (!wanted(level, sys)) return;
  vsnprintf(buf, sizeof buf, fmt, ap);
  mgba_log(level == DEBUG_ERROR ? 1 : (level == DEBUG_INFO ? 3 : 4), "%s: %s", sys, buf);
}

void debug_full(const char *file, const char *func, int line, int level, const char *sys, const char *fmt, ...) {
  va_list ap;
  if (!wanted(level, sys)) return;
  va_start(ap, fmt);
  debugva_full(file, func, line, level, sys, fmt, ap);
  va_end(ap);
}

void debug_errno_full(const char *file, const char *func, int line, const char *sys, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debugva_full(file, func, line, DEBUG_ERROR, sys, fmt, ap);
  va_end(ap);
}

void debug_bytes_offset_full(const char *file, const char *func, int line, int level, const char *sys, unsigned char *buf, int len, unsigned int offset) {
  char s[80];
  int i, j;
  (void)file; (void)func; (void)line;
  if (!wanted(level, sys)) return;
  for (i = 0; i < len; i += 16) {
    int n = snprintf(s, sizeof s, "%04x:", (unsigned)(offset + i));
    for (j = 0; j < 16 && i + j < len; j++) n += snprintf(s + n, sizeof s - n, " %02x", buf[i + j]);
    mgba_log(level == DEBUG_ERROR ? 1 : 3, "%s: %s", sys, s);
  }
}

void debug_bytes_full(const char *file, const char *func, int line, int level, const char *sys, unsigned char *buf, int len) {
  debug_bytes_offset_full(file, func, line, level, sys, buf, len, 0);
}
