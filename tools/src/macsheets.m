// The macOS window's sheets, tested without a window anyone can see:
//
//   tools/macsheets.sh [outdir]
//
// The pause menu, the sheet that draws the game's dialogs and the level pack
// sheet (src/liblsdl2/liblsdl2_mac*.m) are compiled straight into this, against
// a small simulated game that answers the same symbols libpumpkin does. The
// window is titled -- the level controls live in its title bar -- but refuses
// to be moved onto a screen and sits far off every one of them; the process is
// never allowed to become the active application; and every key press is an
// NSEvent handed to the sheet directly, so nothing reaches the desktop, the
// pointer or the keyboard focus of whoever is using the machine. The app's
// preferences are the user's, so the suite the sheets keep things in is swapped
// for a throwaway one, checked before anything is written, and removed after.
// Snapshots of each sheet go to outdir, transparent where the sheet is.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include "liblsdl2_mac.h"
#include "liblsdl2_mac_keys.h"

// The app's preferences are the user's: every suite this code opens is swapped
// for a throwaway one, which is deleted at the end.
static NSString *const TestSuite = @"local.bikeordie2.harness";
@implementation NSUserDefaults (HarnessSuite)
- (instancetype)harness_initWithSuiteName:(NSString *)name {
  if ([name isEqualToString:@"local.bikeordie2"]) name = TestSuite;
  return [self harness_initWithSuiteName:name];
}
@end

@interface OffscreenWindow : NSWindow
@end
@implementation OffscreenWindow
- (NSRect)constrainFrameRect:(NSRect)frame toScreen:(NSScreen *)screen { return frame; }
@end

static const char *OUT;
static int failures = 0;
static NSWindow *window;
static NSMutableArray<NSString *> *commands;

static void check(NSString *what, BOOL ok, id detail) {
  printf("%s %s%s\n", ok ? "ok  " : "FAIL", what.UTF8String,
         detail ? [[@"  -- " stringByAppendingString:[detail description]] UTF8String] : "");
  if (!ok) failures++;
}

// ------------------------------------------------------------ liblsdl2.c stubs
void liblsdl2_log(const char *msg) { fprintf(stderr, "  [log] %s\n", msg); }
static int screen_mode = 2;
int liblsdl2_screen_get(void) { return screen_mode; }
void liblsdl2_screen_set(int m) { screen_mode = m; }
int liblsdl2_screen_from_env(void) { return 1; }
int screen_mode_named(const char *n) { return n == NULL ? -1 : !strcmp(n, "pixels") ? 0 : !strcmp(n, "smooth") ? 1 : !strcmp(n, "xbr") ? 2 : -1; }
const char *screen_mode_name(int m) { return m == 0 ? "pixels" : m == 1 ? "smooth" : "xbr"; }

// ------------------------------------------------------------- the game
static char state[8192] = "ready\nform 0\n";
static uint32_t state_seq = 2, cmd_seq = 0, taken = 0;
static char pending[1024];
static int paused = 0;
static char keys[256] = "forward=w brake=s left=a right=d flip=space";
static char pack_state[2048] = "idle\ntitle BOD - Introduction\n";
static uint32_t pack_seq = 2;
static int check3d = 1, sel_list = 0;
static NSString *form_name = @"none";

static const char *MENUBAR =
  "menu 0 Game\nmitem 0 0 100 Restart Level\nmitem 0 1 101 Previous Level\nmitem 0 2 102 Next Level\nmitem 0 3 110 Select Level\n"
  "mitem 0 4 0 -\nmitem 0 5 123 Player Profiles\nmitem 0 6 120 Pause\nmitem 0 7 121 Statistics\nmitem 0 8 0 -\nmitem 0 9 199 Send \"Bike or Die!\"\nmitem 0 10 198 Quit\n"
  "menu 1 Rec\nmitem 1 0 201 Recorded Games\nmitem 1 1 202 Replay the Last Game\n"
  "menu 2 Options\nmitem 2 0 307 Bike\nmitem 2 1 301 Control\nmitem 2 2 302 Display\nmitem 2 3 303 Sound\nmitem 2 4 304 Recording\nmitem 2 5 305 Hall of Fame\nmitem 2 6 0 -\nmitem 2 7 311 Toggle 3D\n"
  "menu 3 Help\nmitem 3 0 401 Control\nmitem 3 1 402 Rules\nmitem 3 2 403 Hall of Fame\nmitem 3 3 0 -\nmitem 3 4 410 About\xe2\x80\xa6\n";

static void publish(NSString *text) {
  state_seq++;
  snprintf(state, sizeof(state), "%s", text.UTF8String);
  state_seq++;
}

static void show(NSString *name) {
  form_name = name;
  if ([name isEqualToString:@"none"]) {
    publish(@"ready\nform 0\n");
  } else if ([name isEqualToString:@"sound"]) {
    publish(@"ready\nform 1600 1 156 156\ntitle Sound Options\ndefault 2\n"
             "obj 1 button 10 3 0 0 0 8 140 40 12 OK\nobj 2 button 11 7 0 0 0 63 140 40 12 Cancel\n"
             "obj 6 label 1 1 0 0 0 3 16 40 11 Volume:\n"
             "obj 7 push 20 3 0 1 0 45 16 30 12 Mute\nobj 8 push 21 3 0 1 0 76 16 40 12 Custom\nobj 9 push 22 3 1 1 0 117 16 38 12 Automatic\n");
  } else if ([name isEqualToString:@"display"]) {
    publish([NSString stringWithFormat:@"ready\nform 1700 1 156 156\ntitle Display Options\ndefault 2\n"
             "obj 1 button 10 3 0 0 0 8 140 40 12 OK\nobj 2 button 11 7 0 0 0 63 140 40 12 Cancel\n"
             "obj 9 check 27 3 1 0 0 1 28 60 12 Full screen\nobj 10 check 25 3 %d 0 0 75 28 29 12 3D\n"
             "obj 13 label 2 1 0 0 0 32 55 26 11 Zoom:\nobj 14 popup 50 3 0 0 60 58 55 30 12 75%%\n"
             "obj 15 list 60 2 3 0 50 57 54 37 90 \nitems 15 4 %d\nitem 15 0 150%%\nitem 15 1 100%%\nitem 15 2 75%%\nitem 15 3 50%%\n",
             check3d, sel_list]);
  } else if ([name isEqualToString:@"levels"]) {
    publish([NSString stringWithFormat:@"ready\nform 1200 1 156 140\ntitle BOD - Introduction\ndefault 8\n"
             "obj 1 list 10 3 0 0 0 2 26 152 79 \nitems 1 4 %d\nitem 1 0 1. Turn around\nitem 1 1 2. Ice Cold\nitem 1 2 3. Big Jump\nitem 1 3 4. Flying\n"
             "obj 6 button 20 3 0 0 0 3 121 80 16 Play this level\nobj 7 button 22 3 0 0 0 87 121 32 16 More...\nobj 8 button 21 7 0 0 0 122 121 32 16 Cancel\n", sel_list]);
  }
}

static void take(NSString *cmd) {
  NSArray *w = [cmd componentsSeparatedByString:@" "];
  if ([cmd isEqualToString:@"close"]) { show(@"none"); return; }
  if ([w[0] isEqualToString:@"pick"]) {
    if ([cmd isEqualToString:@"pick 2 3"]) show(@"sound");
    else if ([cmd isEqualToString:@"pick 2 2"]) { sel_list = 2; show(@"display"); }
    return;
  }
  if ([w[0] isEqualToString:@"select"]) { sel_list = [w[2] intValue]; show(form_name); return; }
  if ([w[0] isEqualToString:@"press"]) {
    int i = [w[1] intValue];
    if ([form_name isEqualToString:@"levels"]) { if (i == 6 || i == 8) show(@"none"); return; }
    if (i == 1 || i == 2) { show(@"none"); return; }
    if (i == 10) { check3d = !check3d; show(form_name); }
    return;
  }
}

uint32_t bod_ui_command(const char *cmd) {
  NSString *c = [NSString stringWithUTF8String:cmd];
  [commands addObject:[c stringByReplacingOccurrencesOfString:@"\n" withString:@"; "]];
  if (!strncmp(cmd, "pause ", 6)) { paused = atoi(cmd + 6); return 0; }
  snprintf(pending, sizeof(pending), "%s", cmd);
  cmd_seq++;
  // The game takes it a moment later, a line at a time.
  uint32_t seq = cmd_seq;
  bod_keys_after(0.04, ^{
    if (seq != cmd_seq) printf("FAIL command %u was overwritten before it was taken\n", seq), failures++;
    for (NSString *line in [c componentsSeparatedByString:@"\n"]) take(line);
    taken = seq;
  });
  return cmd_seq;
}
uint32_t bod_ui_taken(void) { return taken; }
const char *bod_ui_state(void) { return state; }
uint32_t bod_ui_seq(void) { return state_seq; }
const char *bod_ui_menubar(void) { return MENUBAR; }
uint32_t bod_ui_menubar_seq(void) { return 2; }

static void pack_publish(const char *text) { pack_seq++; snprintf(pack_state, sizeof(pack_state), "%s", text); pack_seq++; }
void bod_pack_command(const char *cmd) {
  [commands addObject:[@"pack " stringByAppendingString:@(cmd)]];
  NSString *c = @(cmd);
  pack_publish("busy\ntitle BOD - Introduction\n");
  bod_keys_after(0.15, ^{
    if ([c isEqualToString:@"open"]) pack_publish("listed\ntitle BOD - Introduction\nIntroduction\nStandard\nNew Deal\nSupernatural\n");
    else if ([c isEqualToString:@"menu Select Level"]) { pack_publish("done\ntitle BOD - Introduction\n"); sel_list = 0; show(@"levels"); }
    else pack_publish("done\ntitle BOD - Introduction\n");
  });
}
const char *bod_pack_state(void) { return pack_state; }
uint32_t bod_pack_seq(void) { return pack_seq; }
void pumpkin_set_ride_keys(const char *spec) {
  [commands addObject:[@"keys " stringByAppendingString:@(spec)]];
  NSString *s = @(spec);
  if ([s rangeOfString:@" "].location != NSNotFound) { snprintf(keys, sizeof(keys), "%s", spec); return; }
  NSArray *kv = [s componentsSeparatedByString:@"="];
  NSMutableArray *parts = [[@(keys) componentsSeparatedByString:@" "] mutableCopy];
  for (NSUInteger i = 0; i < parts.count; i++) if ([parts[i] hasPrefix:[kv[0] stringByAppendingString:@"="]]) parts[i] = s;
  snprintf(keys, sizeof(keys), "%s", [parts componentsJoinedByString:@" "].UTF8String);
}
char *pumpkin_get_ride_keys(void) { return keys; }

// ------------------------------------------------------------ driving
static void pump(NSTimeInterval t) {
  NSDate *until = [NSDate dateWithTimeIntervalSinceNow:t];
  while ([until timeIntervalSinceNow] > 0) {
    NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01] inMode:NSDefaultRunLoopMode dequeue:YES];
    if (e) [NSApp sendEvent:e];
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
  }
}

static NSString *chars(unsigned short code) {
  unichar c;
  switch (code) {
    case BODKeyUp: c = NSUpArrowFunctionKey; break;
    case BODKeyDown: c = NSDownArrowFunctionKey; break;
    case BODKeyLeft: c = NSLeftArrowFunctionKey; break;
    case BODKeyRight: c = NSRightArrowFunctionKey; break;
    case BODKeyReturn: c = '\r'; break;
    case BODKeyEscape: c = 27; break;
    case BODKeyTab: c = '\t'; break;
    case BODKeySpace: c = ' '; break;
    case 34: c = 'i'; break;
    default: c = '?';
  }
  return [NSString stringWithCharacters:&c length:1];
}

static void key(unsigned short code, int times) {
  for (int n = 0; n < times; n++) {
    NSWindow *w = window.attachedSheet ?: window;
    NSEvent *down = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0
                                 windowNumber:w.windowNumber context:nil characters:chars(code)
                  charactersIgnoringModifiers:chars(code) isARepeat:NO keyCode:code];
    NSEvent *up = [NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0 timestamp:0
                               windowNumber:w.windowNumber context:nil characters:chars(code)
                charactersIgnoringModifiers:chars(code) isARepeat:NO keyCode:code];
    [w sendEvent:down];
    [w sendEvent:up];
    pump(0.08);
  }
}

static void repeatKey(unsigned short code) {
  NSWindow *w = window.attachedSheet ?: window;
  NSEvent *e = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0
                            windowNumber:w.windowNumber context:nil characters:chars(code)
             charactersIgnoringModifiers:chars(code) isARepeat:YES keyCode:code];
  [w sendEvent:e];
  pump(0.08);
}

static void snap(NSString *name) {
  NSWindow *w = window.attachedSheet;
  if (!w) return;
  NSView *v = w.contentView;
  NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
  [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
  NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  [png writeToFile:[NSString stringWithFormat:@"%s/%@.png", OUT, name] atomically:YES];
}

static id pauseMenu(void) {
  return [window.attachedSheet isKindOfClass:BODKeyPanel.class] ? ((BODKeyPanel *)window.attachedSheet).keyTarget : nil;
}
static NSString *pauseFocus(void) {
  id m = pauseMenu();
  if (![NSStringFromClass([m class]) isEqualToString:@"BODPauseMenu"]) return @"(not the pause menu)";
  id row = m ? [m valueForKey:@"focus"] : nil;
  return row ? [row valueForKey:@"key"] : @"(none)";
}
static NSString *pausePage(void) { id m = pauseMenu(); return m ? [m valueForKey:@"page"] : @"(no menu)"; }
static NSString *sheetKind(void) {
  id t = [window.attachedSheet isKindOfClass:BODKeyPanel.class] ? ((BODKeyPanel *)window.attachedSheet).keyTarget : nil;
  if (t) return NSStringFromClass([t class]);
  return window.attachedSheet ? @"other sheet" : @"none";
}
static NSString *formFocus(void) {
  id t = pauseMenu();
  if (![NSStringFromClass([t class]) isEqualToString:@"BODFormSheet"]) return @"(no form)";
  NSView *v = [t valueForKey:@"focus"];
  if (!v) return @"(nothing)";
  NSString *title = [v respondsToSelector:@selector(title)] ? [(id)v title] : @"";
  return [NSString stringWithFormat:@"%@ %ld %@", NSStringFromClass(v.class), (long)v.tag, title];
}
static BOOL sawCommand(NSString *c) { return [commands containsObject:c]; }
static BOOL waitFor(BOOL (^cond)(void), NSTimeInterval max) {
  NSDate *until = [NSDate dateWithTimeIntervalSinceNow:max];
  while ([until timeIntervalSinceNow] > 0) { if (cond()) return YES; pump(0.05); }
  return cond();
}

int main(int argc, char **argv) {
  @autoreleasepool {
    OUT = argc > 1 ? argv[1] : "/tmp";
    commands = [NSMutableArray array];
    method_exchangeImplementations(class_getInstanceMethod(NSUserDefaults.class, @selector(initWithSuiteName:)),
                                   class_getInstanceMethod(NSUserDefaults.class, @selector(harness_initWithSuiteName:)));
    {
      NSUserDefaults *probe = [[NSUserDefaults alloc] initWithSuiteName:@"local.bikeordie2"];
      [probe setObject:@"probe" forKey:@"HarnessProbe"];
      BOOL isolated = [[[NSUserDefaults alloc] initWithSuiteName:TestSuite] stringForKey:@"HarnessProbe"] != nil;
      [probe removeObjectForKey:@"HarnessProbe"];
      if (!isolated) { printf("preferences are not isolated; stopping\n"); return 2; }
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    NSMenu *bar = [[NSMenu alloc] init];
    NSMenuItem *win = [[NSMenuItem alloc] initWithTitle:@"Window" action:NULL keyEquivalent:@""];
    win.submenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [bar addItem:win];
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = win.submenu;
    [NSApp finishLaunching];

    window = [[OffscreenWindow alloc] initWithContentRect:NSMakeRect(-6000, -6000, 960, 960)
                                         styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                                           backing:NSBackingStoreBuffered defer:NO];
    [window setFrameOrigin:NSMakePoint(-6000, -6000)];
    [window orderFrontRegardless];
    printf("window at %s, active=%d\n", NSStringFromRect(window.frame).UTF8String, NSApp.isActive);
    for (NSScreen *screen in NSScreen.screens) {
      if (NSIntersectsRect(window.frame, screen.frame)) {
        printf("the window would be on a screen; stopping\n");
        [window orderOut:nil];
        return 2;
      }
    }

    liblsdl2_mac_attach_window((__bridge void *)window);
    pump(0.8);

    // ---- Esc pauses, and the menu comes down on Resume
    check(@"Esc from the game opens the pause menu", liblsdl2_mac_escape() == 1, nil);
    pump(0.3);
    check(@"the game is held", paused == 1 && sawCommand(@"pause 1"), commands);
    check(@"the sheet is the pause menu", [sheetKind() isEqualToString:@"BODPauseMenu"], sheetKind());
    check(@"Resume has the keyboard", [pauseFocus() isEqualToString:@"resume"], pauseFocus());
    check(@"Esc again does not open a second one", liblsdl2_mac_escape() == 0, nil);
    snap(@"m0_main");

    key(BODKeyDown, 1);
    check(@"Down moves to Restart level", [pauseFocus() isEqualToString:@"restart"], pauseFocus());
    key(BODKeyUp, 2);
    check(@"Up from Resume wraps to Help", [pauseFocus() isEqualToString:@"page:help"], pauseFocus());
    repeatKey(BODKeyUp);
    check(@"a repeat of a key held from before is ignored", [pauseFocus() isEqualToString:@"page:help"], pauseFocus());
    key(BODKeyUp, 3);
    check(@"Up x3 reaches Controls", [pauseFocus() isEqualToString:@"page:controls"], pauseFocus());

    // ---- Controls: bind Forward to I
    key(BODKeyReturn, 1);
    check(@"Return opens Controls", [pausePage() isEqualToString:@"controls"], pausePage());
    check(@"Forward has the keyboard", [pauseFocus() isEqualToString:@"bind:forward"], pauseFocus());
    key(BODKeyReturn, 1);
    snap(@"m1_listening");
    key(34, 1);   // i
    check(@"I is bound to Forward", sawCommand(@"keys forward=i") && strstr(keys, "forward=i") != NULL, @(keys));
    id row = [pauseMenu() valueForKey:@"focus"];
    check(@"the row shows it", [[row valueForKey:@"detail"] isEqualToString:@"I"], [row valueForKey:@"detail"]);
    snap(@"m2_controls");
    key(BODKeyDown, 5);
    check(@"Down x5 reaches Default keys", [pauseFocus() isEqualToString:@"default-keys"], pauseFocus());
    key(BODKeyReturn, 1);
    check(@"Default keys puts W back", strstr(keys, "forward=w") != NULL, @(keys));
    key(BODKeyEscape, 1);
    check(@"Esc goes back to the first page, on Controls", [pausePage() isEqualToString:@"main"] && [pauseFocus() isEqualToString:@"page:controls"], pauseFocus());

    // ---- Settings: screen, and the game's Sound dialog and back
    key(BODKeyDown, 1);
    key(BODKeyReturn, 1);
    check(@"Settings open on Screen", [pausePage() isEqualToString:@"settings"] && [pauseFocus() isEqualToString:@"screen"], pauseFocus());
    key(BODKeyLeft, 1);
    check(@"Left picks Smooth", screen_mode == 1, @(screen_mode));
    key(BODKeyRight, 1);
    check(@"Right picks xBR again", screen_mode == 2, @(screen_mode));
    snap(@"m3_settings");
    key(BODKeyDown, 1);
    check(@"Down reaches the game's Bike options", [pauseFocus() isEqualToString:@"menu:2 0"], pauseFocus());
    key(BODKeyDown, 3);
    check(@"Down x3 reaches Sound", [pauseFocus() isEqualToString:@"menu:2 3"], pauseFocus());
    key(BODKeyReturn, 1);
    check(@"Sound's dialog comes up", waitFor(^{ return [sheetKind() isEqualToString:@"BODFormSheet"]; }, 2), sheetKind());
    check(@"it was asked for, and not held back", sawCommand(@"pick 2 3"), commands);
    check(@"the game is still held", paused == 1, nil);
    pump(0.2);
    snap(@"d0_sound");
    check(@"the dialog's first control has the keyboard", [formFocus() hasPrefix:@"NSSegmentedControl"], formFocus());
    key(BODKeyLeft, 1);
    pump(0.2);
    check(@"Left on Volume presses the push button before", [commands.lastObject isEqualToString:@"press 8"], commands.lastObject);
    key(BODKeyDown, 1);
    check(@"Down goes to the buttons", [formFocus() hasPrefix:@"NSButton"], formFocus());
    key(BODKeyEscape, 1);
    check(@"Esc closes it and comes back to Settings", waitFor(^{ return [sheetKind() isEqualToString:@"BODPauseMenu"]; }, 2) && [pausePage() isEqualToString:@"settings"], pausePage());
    check(@"on Sound", [pauseFocus() isEqualToString:@"menu:2 3"], pauseFocus());

    // ---- Display: a checkbox toggles
    key(BODKeyUp, 1);
    key(BODKeyReturn, 1);
    check(@"Display's dialog comes up", waitFor(^{ return [sheetKind() isEqualToString:@"BODFormSheet"]; }, 2), sheetKind());
    pump(0.2);
    for (int i = 0; i < 6 && ![formFocus() containsString:@"3D"]; i++) key(BODKeyTab, 1);
    check(@"Tab reaches 3D", [formFocus() containsString:@"3D"], formFocus());
    key(BODKeySpace, 1);
    pump(0.4);
    check(@"Space ticks it off in the game", check3d == 0, @(check3d));
    NSView *v = [pauseMenu() valueForKey:@"focus"];
    check(@"and on the sheet", [v isKindOfClass:NSButton.class] && ((NSButton *)v).state == NSControlStateValueOff, v);
    snap(@"d1_display");
    for (int i = 0; i < 6 && ![formFocus() hasPrefix:@"NSPopUpButton"]; i++) key(BODKeyTab, 1);
    key(BODKeyRight, 1);
    pump(0.3);
    check(@"Right on the Zoom pop-up picks the next", sel_list == 3, @(sel_list));
    key(BODKeyEscape, 1);
    check(@"Cancel comes back", waitFor(^{ return [sheetKind() isEqualToString:@"BODPauseMenu"]; }, 2), sheetKind());

    // ---- Choose a level: Return chooses, moves to Play this level, Return plays
    key(BODKeyEscape, 1);
    check(@"Esc to the first page", [pausePage() isEqualToString:@"main"], pausePage());
    key(BODKeyUp, 3);
    check(@"Up x3 reaches Choose a level", [pauseFocus() isEqualToString:@"levels"], pauseFocus());
    key(BODKeyReturn, 1);
    check(@"the level list comes up", waitFor(^{ return [sheetKind() isEqualToString:@"BODFormSheet"]; }, 2), commands);
    pump(0.2);
    snap(@"d2_levels");
    check(@"the list has the keyboard", [formFocus() hasPrefix:@"NSTableView"], formFocus());
    key(BODKeyDown, 2);
    pump(0.3);
    check(@"Down walks the list and the game hears it", sel_list == 2, @(sel_list));
    key(BODKeyReturn, 1);
    check(@"Return moves on to Play this level, not Cancel", [formFocus() containsString:@"Play this level"], formFocus());
    check(@"and pressed nothing", ![commands.lastObject hasPrefix:@"press"], commands.lastObject);
    key(BODKeyReturn, 1);
    check(@"Return plays it and the pause is over",
          waitFor(^{ return (BOOL)(paused == 0 && window.attachedSheet == nil); }, 3), @{ @"paused": @(paused), @"sheet": sheetKind() });
    check(@"Play was pressed", [commands containsObject:@"press 6"] || [[commands componentsJoinedByString:@"|"] containsString:@"press 6"], commands);

    // ---- Level packs from the pause menu, and back without a choice
    check(@"Esc pauses again", liblsdl2_mac_escape() == 1, nil);
    pump(0.3);
    key(BODKeyDown, 5);
    check(@"Down x5 reaches Level packs", [pauseFocus() isEqualToString:@"packs"], pauseFocus());
    key(BODKeyReturn, 1);
    check(@"the pack sheet comes up", waitFor(^{ return [sheetKind() isEqualToString:@"other sheet"]; }, 2), sheetKind());
    pump(0.5);
    snap(@"p0_packs");
    for (NSWindow *w in @[ window.attachedSheet ?: window ]) {
      NSButton *cancel = nil;
      NSMutableArray *stack = [NSMutableArray arrayWithObject:w.contentView];
      while (stack.count) { NSView *x = stack.lastObject; [stack removeLastObject]; if ([x isKindOfClass:NSButton.class] && [((NSButton *)x).title isEqualToString:@"Cancel"]) cancel = (NSButton *)x; [stack addObjectsFromArray:x.subviews]; }
      [cancel performClick:nil];
    }
    check(@"Cancel comes back to the pause menu, on Level packs",
          waitFor(^{ return [sheetKind() isEqualToString:@"BODPauseMenu"]; }, 2) && [pauseFocus() isEqualToString:@"packs"], pauseFocus());
    check(@"still paused", paused == 1, nil);
    Uint32 pressed = SDL_GetTicks();
    key(BODKeyEscape, 1);
    check(@"Esc resumes", waitFor(^{ return (BOOL)(paused == 0 && window.attachedSheet == nil); }, 2), sheetKind());
    {
      // SDL hands that Escape over after the sheet has gone: it is still the window's.
      check(@"the Escape that resumed is not the game's, or a new pause", liblsdl2_mac_wants_key(pressed) == 1, nil);
      pump(0.05);
      check(@"a key pressed after resuming is the game's", liblsdl2_mac_wants_key(SDL_GetTicks()) == 0, nil);
    }

    // ---- Putting the window away pauses
    [[NSNotificationCenter defaultCenter] postNotificationName:NSWindowWillMiniaturizeNotification object:window];
    pump(0.3);
    check(@"minimizing pauses", paused == 1 && [sheetKind() isEqualToString:@"BODPauseMenu"], sheetKind());
    snap(@"m4_after_minimize");
    key(BODKeyEscape, 1);
    pump(0.3);
    check(@"and Esc resumes", paused == 0, nil);

    [[NSUserDefaults standardUserDefaults] removePersistentDomainForName:TestSuite];
    printf("%s\n", failures ? [NSString stringWithFormat:@"%d FAILED", failures].UTF8String : "all passed");
  }
  return failures ? 1 : 0;
}
