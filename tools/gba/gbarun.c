/* Headless mGBA runner for the Bike or Die 2 GBA port.
 *
 * Loads a ROM into libmgba, drives it from a plain-text script with the same
 * vocabulary as tools/headless.sh (wait, key, keydown, keyup, shot), writes
 * the requested frames as PNG, and prints everything the ROM says through
 * mGBA's debug port along with the emulator's own warnings (bad memory
 * accesses, undefined instructions).
 *
 *   gbarun -r rom.gba [-s script.txt] [-o outdir] [-t seconds] [-p ms] [-S save.sav] [-v]
 *
 * Keys: a b select start right left up down r l.
 * "gbarun: exit" printed by the ROM ends the run early; "gbarun: fail" also
 * makes the exit status non-zero. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/config.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/vfs.h>
#define USE_PNG 1
#include <png.h>
#include <mgba-util/png-io.h>

static unsigned frame_no;
static int verbose;
static int want_exit, failed;
static FILE *outlog;

static struct mCore *core;
static void hlog(struct mLogger *l, int cat, enum mLogLevel lvl, const char *fmt, va_list args) {
  const char *name = mLogCategoryName(cat);
  char buf[2048];
  (void)l;
  if (!verbose && (lvl & (mLOG_DEBUG | mLOG_STUB)) && strcmp(name, "GBA Debug") != 0) return;
  vsnprintf(buf, sizeof buf, fmt, args);
  if (!strncmp(buf, "Starting DMA", 12)) return;
  {
    /* collapse floods of the same kind of complaint */
    static char lastkind[64]; static int repeats;
    char kind[64];
    int i;
    for (i = 0; i < 63 && buf[i] && buf[i] != ':'; i++) kind[i] = buf[i];
    kind[i] = 0;
    if (!strncmp(buf, "Illegal opcode", 14)) {
      static int shown;
      if (shown++ == 0) {
        int32_t pc = 0, lr = 0, sp = 0, r0 = 0, r1 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0;
        core->readRegister(core, "pc", &pc); core->readRegister(core, "r14", &lr); core->readRegister(core, "sp", &sp);
        core->readRegister(core, "r0", &r0); core->readRegister(core, "r1", &r1); core->readRegister(core, "r4", &r4);
        core->readRegister(core, "r5", &r5); core->readRegister(core, "r6", &r6); core->readRegister(core, "r7", &r7);
        fprintf(outlog, "[%06u gbarun] illegal opcode at pc=0x%08X lr=0x%08X sp=0x%08X r0=0x%08X r1=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X\n", frame_no, pc, lr, sp, r0, r1, r4, r5, r6, r7);
        {
          /* the stack around sp: return addresses of the callers */
          int i; uint32_t w;
          fprintf(outlog, "[%06u gbarun]   last syscall: group %u function 0x%03X engine lr 0x%08X r0 0x%08X\n", frame_no,
                  core->busRead32(core, 0x030053F0), core->busRead32(core, 0x030053F4), core->busRead32(core, 0x030053F8), core->busRead32(core, 0x030053FC));
          for (i = -16; i < 64; i++) { w = core->busRead32(core, (uint32_t)sp + 4 * i); if ((w >> 24) == 0x08 || (w >> 24) == 0x03 || (w >> 24) == 0x02) fprintf(outlog, "[%06u gbarun]   stack[%d] = 0x%08X\n", frame_no, i, w); }
        }
      }
      if (shown > 3) return;
    }
    if (!strncmp(buf, "Bad ", 4) && !strcmp(kind, lastkind)) {
      if (++repeats > 8) { if (repeats == 9) fprintf(outlog, "[%06u %s] ... (more of the same suppressed)\n", frame_no, name); return; }
    } else { strcpy(lastkind, kind); repeats = 0; }
  }
  fprintf(outlog, "[%06u %s] %s\n", frame_no, name, buf);
  fflush(outlog);
  if (strncmp(buf, "gbarun: exit", 12) == 0) want_exit = 1;
  if (strncmp(buf, "gbarun: fail", 12) == 0) { want_exit = 1; failed = 1; }
}

static const char *outdir;
static void dump_mem(uint32_t addr, uint32_t len, const char *name) {
  char path[512]; FILE *f; uint32_t i;
  snprintf(path, sizeof path, "%s/%s", outdir, name);
  if ((f = fopen(path, "wb")) == NULL) return;
  for (i = 0; i < len; i += 4) { uint32_t w = core->busRead32(core, addr + i); fwrite(&w, 4, 1, f); }
  fclose(f);
  fprintf(outlog, "[%06u dump] %s: 0x%08X %u bytes\n", frame_no, path, addr, len);
}

static int keynum(const char *s) {
  static const char *names[] = { "a", "b", "select", "start", "right", "left", "up", "down", "r", "l" };
  for (int i = 0; i < 10; i++) if (!strcmp(s, names[i])) return i;
  return -1;
}

struct act { int op; int a, b; char label[64]; };
enum { OP_WAIT, OP_KEY, OP_KEYDOWN, OP_KEYUP, OP_SHOT, OP_TAP, OP_DUMP };

static int load_script(const char *path, struct act *acts, int max) {
  FILE *f = fopen(path, "r");
  char line[256];
  int n = 0;
  if (!f) { perror(path); return -1; }
  while (fgets(line, sizeof line, f) && n < max) {
    char w0[64] = "", w1[64] = "", w2[64] = "";
    if (line[0] == '#' || sscanf(line, "%63s %63s %63s", w0, w1, w2) < 1) continue;
    struct act *a = &acts[n];
    memset(a, 0, sizeof *a);
    if (!strcmp(w0, "wait"))         { a->op = OP_WAIT; a->a = atoi(w1); }
    else if (!strcmp(w0, "key"))     { a->op = OP_KEY; a->a = keynum(w1); a->b = w2[0] ? atoi(w2) : 6; }
    else if (!strcmp(w0, "keydown")) { a->op = OP_KEYDOWN; a->a = keynum(w1); }
    else if (!strcmp(w0, "keyup"))   { a->op = OP_KEYUP; a->a = keynum(w1); }
    else if (!strcmp(w0, "shot"))    { a->op = OP_SHOT; strncpy(a->label, w1, sizeof a->label - 1); }
    else if (!strcmp(w0, "dump"))    { char w3[64] = ""; sscanf(line, "%*s %*s %*s %63s", w3); a->op = OP_DUMP; a->a = (int)strtoul(w1, NULL, 0); a->b = (int)strtoul(w2, NULL, 0); strncpy(a->label, w3, sizeof a->label - 1); }
    else if (!strcmp(w0, "tap"))     { a->op = OP_TAP; a->a = atoi(w1); a->b = atoi(w2); }
    else { fprintf(stderr, "script: unknown command %s\n", w0); continue; }
    if ((a->op == OP_KEY || a->op == OP_KEYDOWN || a->op == OP_KEYUP) && a->a < 0) {
      fprintf(stderr, "script: unknown key %s\n", w1); continue;
    }
    n++;
  }
  fclose(f);
  return n;
}

static struct mCore *core;
static color_t *vbuf;
static unsigned vw, vh;
static const char *outdir = ".";
static uint32_t keys;
static int period_frames, shot_seq;
static int profile, profile_from;
#define NSAMPLES 65536
static uint32_t samples[NSAMPLES], samples_lr[NSAMPLES];
static int nsamples;

static void shot(const char *label) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s.png", outdir, label);
  struct VFile *vf = VFileOpen(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!vf) { perror(path); return; }
  png_structp png = PNGWriteOpen(vf);
  png_infop info = PNGWriteHeader(png, vw, vh);
  PNGWritePixels(png, vw, vh, vw, vbuf);
  PNGWriteClose(png, info);
  vf->close(vf);
  fprintf(outlog, "[%06u shot] %s\n", frame_no, path);
}

static void run_frames(int n) {
  while (n-- > 0 && !want_exit) {
    core->setKeys(core, keys);
    core->runFrame(core);
    frame_no++;
    if (profile && (int)frame_no >= profile_from) {
      /* sample the program counter a few times per frame by stepping a little */
      int k;
      /* samples spread at random over the frame's first ~100k instructions,
       * so a workload with a period near the frame's cannot alias */
      for (k = 0; k < 16 && nsamples < NSAMPLES; k++) {
        int32_t pc = 0, lr = 0, m, n = 500 + (rand() % 12000);
        for (m = 0; m < n; m++) core->step(core);
        core->readRegister(core, "pc", &pc);
        core->readRegister(core, "r14", &lr);
        samples_lr[nsamples] = (uint32_t)lr;
        samples[nsamples++] = (uint32_t)pc;
      }
    }
    if (period_frames && frame_no % period_frames == 0) {
      char label[32];
      snprintf(label, sizeof label, "f%05d", shot_seq++);
      shot(label);
    }
  }
}

static int ms_to_frames(int ms) { return (int)((ms * 59.7275) / 1000.0 + 0.5); }

int main(int argc, char **argv) {
  const char *rom = NULL, *script = NULL, *save = NULL;
  int max_seconds = 0, opt;
  outlog = stdout;
  while ((opt = getopt(argc, argv, "r:s:o:t:p:S:vxy:")) != -1) {
    switch (opt) {
      case 'r': rom = optarg; break;
      case 's': script = optarg; break;
      case 'o': outdir = optarg; break;
      case 't': max_seconds = atoi(optarg); break;
      case 'p': period_frames = ms_to_frames(atoi(optarg)); break;
      case 'S': save = optarg; break;
      case 'v': verbose = 1; break;
      case 'x': profile = 1; break;
      case 'y': profile_from = atoi(optarg); break;
      default: fprintf(stderr, "usage: gbarun -r rom.gba [-s script] [-o outdir] [-t seconds] [-p ms] [-S save] [-v]\n"); return 2;
    }
  }
  if (!rom) { fprintf(stderr, "gbarun: -r rom.gba required\n"); return 2; }

  static struct mLogger logger = { .log = hlog };
  mLogSetDefaultLogger(&logger);

  core = mCoreFind(rom);
  if (!core) { fprintf(stderr, "gbarun: not a GBA ROM: %s\n", rom); return 1; }
  core->init(core);
  mCoreInitConfig(core, "gbarun");
  mCoreConfigSetValue(&core->config, "idleOptimization", "ignore");
  mCoreConfigSetValue(&core->config, "skipBios", "1");
  mCoreLoadConfig(core);
  core->desiredVideoDimensions(core, &vw, &vh);
  vbuf = calloc(vw * vh, sizeof(color_t));
  core->setVideoBuffer(core, vbuf, vw);
  if (!mCoreLoadFile(core, rom)) { fprintf(stderr, "gbarun: cannot load %s\n", rom); return 1; }
  if (save) {
    struct VFile *sv = VFileOpen(save, O_RDWR | O_CREAT);
    if (sv) core->loadSave(core, sv);
  }
  core->reset(core);

  struct act acts[4096];
  int n = script ? load_script(script, acts, 4096) : 0;
  if (n < 0) return 1;
  int max_frames = max_seconds ? max_seconds * 60 : 0;
  for (int i = 0; i < n && !want_exit; i++) {
    struct act *a = &acts[i];
    switch (a->op) {
      case OP_WAIT: run_frames(ms_to_frames(a->a)); break;
      case OP_KEY: keys |= 1u << a->a; run_frames(a->b); keys &= ~(1u << a->a); run_frames(2); break;
      case OP_KEYDOWN: keys |= 1u << a->a; run_frames(1); break;
      case OP_KEYUP: keys &= ~(1u << a->a); run_frames(1); break;
      case OP_SHOT: shot(a->label); break;
      case OP_DUMP: dump_mem((uint32_t)a->a, (uint32_t)a->b, a->label); break;
      case OP_TAP: fprintf(outlog, "[%06u script] tap %d %d ignored (no touch screen)\n", frame_no, a->a, a->b); break;
    }
    if (max_frames && (int)frame_no >= max_frames) break;
  }
  if (!script) run_frames(max_frames ? max_frames : 600);
  else if (max_frames && (int)frame_no < max_frames && !want_exit) run_frames(max_frames - frame_no);
  shot("final");
  if (profile && nsamples) {
    /* histogram of sampled program counters in 64-byte buckets, top 40 */
    enum { NB = 8192 };
    static struct { uint32_t addr; int n; } top[NB];
    int ntop = 0, i, j;
    for (i = 0; i < nsamples; i++) {
      uint32_t b = samples[i] & ~255u, h = (b >> 8) & (NB - 1);
      while (top[h].n && top[h].addr != b) h = (h + 1) & (NB - 1);
      if (!top[h].n) { top[h].addr = b; ntop++; }
      top[h].n++;
    }
    for (i = 0; i < NB; i++) for (j = i + 1; j < NB; j++) if (top[j].n > top[i].n) { __typeof__(top[0]) t = top[i]; top[i] = top[j]; top[j] = t; }
    if (ntop > 120) ntop = 120;
    fprintf(outlog, "[profile] %d samples\n", nsamples);
    for (i = 0; i < ntop; i++) fprintf(outlog, "[profile] %5.1f%% 0x%08X\n", 100.0 * top[i].n / nsamples, top[i].addr);
    /* who calls the hottest code: the return address seen with each of the top buckets */
    for (i = 0; i < 8 && i < ntop; i++) {
      struct { uint32_t addr; int n; } cl[64];
      int ncl = 0, k;
      for (j = 0; j < nsamples; j++) {
        uint32_t l;
        if ((samples[j] & ~255u) != top[i].addr) continue;
        l = samples_lr[j] & ~255u;
        for (k = 0; k < ncl; k++) if (cl[k].addr == l) { cl[k].n++; break; }
        if (k == ncl && ncl < 64) { cl[ncl].addr = l; cl[ncl].n = 1; ncl++; }
      }
      for (k = 0; k < ncl; k++) for (j = k + 1; j < ncl; j++) if (cl[j].n > cl[k].n) { __typeof__(cl[0]) t = cl[k]; cl[k] = cl[j]; cl[j] = t; }
      for (k = 0; k < 3 && k < ncl; k++) fprintf(outlog, "[callers] 0x%08X <- 0x%08X %5.1f%%\n", top[i].addr, cl[k].addr, 100.0 * cl[k].n / top[i].n);
    }
  }
  fprintf(outlog, "[%06u gbarun] done, %u frames%s\n", frame_no, frame_no, failed ? " FAILED" : "");
  core->deinit(core);
  return failed ? 1 : 0;
}
