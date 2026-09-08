// Minimal CoreGraphics input injector used to drive the game during testing.
//   macinput click <x> <y>
//   macinput move <x> <y>
//   macinput key <keycode> <down|up|tap>
//   macinput type <string>
//   macinput sleep <ms>
// Coordinates are global screen points.
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void post(CGEventRef e) { CGEventPost(kCGHIDEventTap, e); CFRelease(e); }

static void mouse(double x, double y, int click) {
  CGPoint p = CGPointMake(x, y);
  post(CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, p, kCGMouseButtonLeft));
  usleep(60000);
  if (click) {
    post(CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, p, kCGMouseButtonLeft));
    usleep(90000);
    post(CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, p, kCGMouseButtonLeft));
  }
}

static void key(CGKeyCode k, int down) {
  CGEventRef e = CGEventCreateKeyboardEvent(NULL, k, down);
  post(e);
}

int main(int argc, char **argv) {
  if (argc < 2) return 1;
  const char *cmd = argv[1];
  const char *kd = getenv("MACINPUT_KEY_DELAY_US");
  unsigned kdelay = kd ? (unsigned)atoi(kd) : 90000;
  if (!strcmp(cmd, "winid") && argc >= 3) {
    // CGWindowID of the largest on-screen window owned by a pid, so
    // `screencapture -l` can grab it even when another app overlaps it.
    int want = atoi(argv[2]);
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    long best = -1, bestArea = 0;
    for (CFIndex i = 0; list && i < CFArrayGetCount(list); i++) {
      CFDictionaryRef d = CFArrayGetValueAtIndex(list, i);
      CFNumberRef pidRef = CFDictionaryGetValue(d, kCGWindowOwnerPID);
      int pid = 0; if (pidRef) CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
      if (pid != want) continue;
      CFNumberRef idRef = CFDictionaryGetValue(d, kCGWindowNumber);
      long wid = 0; if (idRef) CFNumberGetValue(idRef, kCFNumberLongType, &wid);
      CFDictionaryRef b = CFDictionaryGetValue(d, kCGWindowBounds);
      CGRect r = CGRectZero;
      if (b) CGRectMakeWithDictionaryRepresentation(b, &r);
      long area = (long)(r.size.width * r.size.height);
      if (area > bestArea) { bestArea = area; best = wid; }
    }
    if (list) CFRelease(list);
    if (best < 0) return 2;
    printf("%ld\n", best);
  }
  else if (!strcmp(cmd, "click") && argc >= 4)      mouse(atof(argv[2]), atof(argv[3]), 1);
  else if (!strcmp(cmd, "move") && argc >= 4)  mouse(atof(argv[2]), atof(argv[3]), 0);
  else if (!strcmp(cmd, "sleep") && argc >= 3) usleep(atoi(argv[2]) * 1000);
  else if (!strcmp(cmd, "key") && argc >= 4) {
    CGKeyCode k = (CGKeyCode)atoi(argv[2]);
    if (!strcmp(argv[3], "down")) key(k, 1);
    else if (!strcmp(argv[3], "up")) key(k, 0);
    else { key(k, 1); usleep(50000); key(k, 0); }
  } else if (!strcmp(cmd, "type") && argc >= 3) {
    // SDL reads virtual keycodes, so translate ASCII to macOS keycodes.
    static const struct { char c; CGKeyCode k; } M[] = {
      {'a',0},{'s',1},{'d',2},{'f',3},{'h',4},{'g',5},{'z',6},{'x',7},{'c',8},{'v',9},
      {'b',11},{'q',12},{'w',13},{'e',14},{'r',15},{'y',16},{'t',17},{'1',18},{'2',19},
      {'3',20},{'4',21},{'6',22},{'5',23},{'=',24},{'9',25},{'7',26},{'-',27},{'8',28},
      {'0',29},{']',30},{'o',31},{'u',32},{'[',33},{'i',34},{'p',35},{'l',37},{'j',38},
      {'\'',39},{'k',40},{';',41},{'\\',42},{',',43},{'/',44},{'n',45},{'m',46},{'.',47},
      {' ',49},{'`',50},
    };
    for (const char *s = argv[2]; *s; s++) {
      char c = *s; int found = -1;
      for (unsigned i = 0; i < sizeof(M)/sizeof(M[0]); i++) if (M[i].c == c) { found = M[i].k; break; }
      if (found < 0) continue;
      key((CGKeyCode)found, 1); usleep(kdelay);
      key((CGKeyCode)found, 0); usleep(kdelay * 2);
    }
  } else { fprintf(stderr, "bad args\n"); return 1; }
  return 0;
}
