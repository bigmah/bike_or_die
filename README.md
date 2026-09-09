# Bike or Die 2 — native on Apple Silicon

Bike or Die 2 (ToySpring, 2008) was a Palm OS 5 game. This repository turns it into a
native macOS/arm64 application by **statically recompiling the game's own machine code**
— both instruction sets it ships — and running it against a native re-implementation of
the Palm OS API. No Palm ROM and no CPU interpreter are used for the game code.

    open "build/Bike or Die 2.app"

## What the game actually is

`files/BikeOrDie-2.1b.prc` is a Palm OS 5 application, creator `BiKD`, 284 resources:

| resource | size | what it is |
|---|---|---|
| `code` 0-3 | 81 KB | Motorola 68000 code — UI, menus, level/database logic |
| `armc` 1-4 | 199 KB | native **ARM32** code — physics and the span/texture renderer |
| `data`/`rloc` | 1.5 KB | compressed A5 world (globals) + relocation chains |
| `Tbmp`,`BikL`,`tex0`,`Pwav`,`trck`,`tFRM`… | 1 MB | sprites, levels, textures, sound, forms |

It calls **245 distinct Palm OS traps** from 1,493 call sites, including 34
`PceNativeCall`s into the ARM blobs. An M4 cannot execute ARM32 at all, so that half had
to be translated too, not just run.

## How it is built

1. **Palm OS API, natively.** [PumpkinOS](https://github.com/migueletto/PumpkinOS) (GPLv3)
   re-implements the Palm OS API for modern machines. It had no macOS support; this repo
   adds a Darwin/arm64 port (`vendor/PumpkinOS`, see `docs/PORTING.md`).
2. **Static recompilation.** `tools/recomp68k.py` and `tools/recomparm.py` translate the
   game's code to C, which clang compiles to arm64. Every aligned offset in every code
   resource is translated and the generated chunk indexes a label array, so jump tables
   and computed branches need no control-flow recovery:

   | | reachable instructions | translated |
   |---|---|---|
   | 68000 | 23,337 | 23,330 (99.97%; the 7 misses are data bytes) |
   | ARM32 | 23,877 | 23,877 (100%) |

   Each function covers 1 KB of the original code. A native compiler will take far
   larger ones, but these functions are unusual -- a label and a label-array entry per
   instruction -- and WebAssembly will not: past a few hundred instructions clang either
   exceeds the 50,000 locals a wasm function may have or, at `-O1`, produces one that
   misbehaves. `BOD_CHUNK_SLOTS` changes it.

   That becomes ~185k lines of generated C (`src/gen`), ~11 MB of native code.
3. **Bridging.** Traps become direct calls into the Palm OS layer; `PceNativeCall`,
   the ARM→68k trampoline and the PACE syscall addresses are handled by the glue in
   `emupalmos.c`.

## Controls

| key | action |
|---|---|
| **↑** | Forward (pedal) |
| **↓** | Brake |
| **←** / **→** | Balance left / right |
| **Space** | **Flip** -- turn around and ride the other way |
| F1-F4 | the four Palm hardware buttons |
| F5 | Palm menu (Game / Rec / Options / Help) |

Those are the game's own defaults, visible and rebindable under **Options → Control**
(F5, then Options, then Control). Its five actions map to a Palm 5-way navigator:
Forward=Up, Brake=Down, Balance=Left/Right, Flip=Select.

A Palm OS 5 device has no left/right/select in the base key manager, so games poll a
vendor navigator mask instead. Bike or Die keeps its table of available physical keys at
`globals+0x3E74` and expects `keyBitNavLeft`/`NavRight`/`NavSelect`
(0x01000000/0x02/0x04). PumpkinOS only reported its own 0x400/0x800 and nothing at all
for select, so the arrows did nothing and Flip was unreachable; it now reports the
palmOne navigator and Handspring rocker bits, with space and F9 as select.
(`BOD_DUMP_KEYS=1` dumps that table if another game needs the same treatment.)

A browser adds one more translation to the chain, and Safari gets it wrong: macOS sets
`NSEventModifierFlagNumericPad` on the arrow keys, so WebKit reports them with
`KeyboardEvent.location` = numpad, and SDL's Emscripten backend dutifully turns Up into
keypad 8. Nothing downstream had a use for keypad 8, so in Safari the arrows did nothing
at all -- while space, which is not a navigation key, worked. The scancode still says
which physical key it was, so the display driver takes the five-way keys from there.

## Menus, from the keyboard

Every menu and dialog is fully navigable without the mouse, which still works exactly as
it did:

| key | in a dialog | in the F5 menu |
|---|---|---|
| **↑** / **↓** | move the focus ring; inside a list, move the selection and step out at its ends | move down the items |
| **←** / **→** | move the focus ring | change pull-down |
| **Enter** / **Space** / F9 | press what the ring is on; with no ring, the dialog's default button | pick the highlighted item |
| **Esc** | close a popup list | close the menu |

A menu or dialog that comes up under the player's hands -- finishing a level puts one up
-- does not take the keys still in flight from riding: it waits for the keyboard to fall
still for half a second first, so the pedal taps do not walk a focus ring around it. One
opened deliberately, from the menu or with a tap, has the keyboard straight away.

This is Palm OS 5's own five-way navigation, which PumpkinOS had not implemented. The
order the ring walks is the form's `fnav` resource where there is one -- Bike or Die
ships ten, one per dialog it expects a Treo's navigator to drive -- and the objects' own
top-to-bottom, left-to-right geometry for the fifteen dialogs without one. Only modal
forms take part, which is every menu and dialog and not the playfield, so the arrow keys
still ride the bike.

The keys are taken in `EvtGetEvent`, before the application sees them, rather than in
`FrmHandleEvent` where Palm OS handles them. A game that plays with the navigator claims
page up and page down -- two of its five keys -- at the top of its own event loop, and
would never pass them on: with the ring fed after the application, three of the five
arrows work in a dialog and the two that pedal the bike do not.

Selecting is a synthesised tap at the middle of the object, so it goes down exactly the
path the pen does and needs nothing from the game. Its release is left pending until the
event queue drains, which is where a lifted pen lands: a release that overtook the enter
event it generated would leave the button drawn stuck down.

## Configuration

The launcher reads `~/.bikeordie.env` if it exists. Useful settings:

| variable | default | meaning |
|---|---|---|
| `PUMPKIN_USER` | `PalmDB` | HotSync user name; the registration code is keyed to it |
| `PUMPKIN_DISPLAY_LE` | `1` | the ARM blitter writes little-endian RGB565 |
| `PUMPKIN_SOUND` | `1` | enable audio (sets both the global and per-app switch) |
| `PUMPKIN_VOLUME` | `64` | Palm sound volumes, 0-64 |
| `PUMPKIN_AUDIO_MS` | `240` | audio queued ahead of the device, in ms |
| `BOD_RECOMP` | `1` | use the statically recompiled cores |
| `BOD_ARM_ENGINE` | `recomp` | `interp` falls back to the ARM interpreter |
| `BOD_RESET` | `0` | `1` re-seeds the game data on next launch |
| `BOD_ZOOM` | `3` | integer window scale; the game itself is 320x320 |
| `BOD_UNLOCK` | `44652` | the registration code to answer the About dialog with; `0` leaves it alone |

Game data (saves, level packs, preferences) lives in
`~/Library/Application Support/Bike or Die 2`.

## Registration

The game is shareware: the About dialog asks for a code keyed to the HotSync user
name, and without one it runs in trial mode. `PUMPKIN_USER` is `PalmDB`, and
44652 is the code for that name.

Typing it in works, and lasts exactly as long as the process does. Bike or Die
keeps its registration in its own application database, which PumpkinOS opens
read-only (`StoLockForReading`, never for writing), so the write goes nowhere: a
session that registered leaves a storage tree byte-identical to one that stayed
in trial mode. Rather than teach it to remember, the port answers the dialog on
the way in. `FrmDrawForm` fills the field and presses the button the first time
it draws a form that has a text field and a button labelled `Unlock` -- found by
what it is rather than by a resource id, so it stops matching by itself once the
game is registered and that button is gone. The game then validates the code
against the user name exactly as it would a typed one, and comes up registered
before the dialog is ever seen. `BOD_UNLOCK=0` leaves it in trial mode; anyone
whose code was issued for a different HotSync name sets both variables.

Two related things now reach the disk that did not. A dirty resource used to be
written only when its database was closed, and an application keeps its own
databases open until it stops -- which a browser tab never does, and a killed
process does not either -- so what it had saved went with it. A resource is now
written when it is released, the way a record always has been, and whatever is
still open at shutdown is flushed.

## Status

Both cores are used by default. The game boots, renders and plays entirely on
statically recompiled code; the ARM interpreter is only kept around as a reference for
differential testing.

## Sound

Three separate things default to silent, and all three have to be on:

- PumpkinOS's **global** sound switch (`prefs.value[pEnableSound]`, default 0),
- the **per-app** registry flag (`regSoundID`, default 0),
- the Palm **volume** preferences (`sysSoundVolume`/`gameSoundVolume`/`alarmSoundVolume`,
  all default 0) -- and Bike or Die's own Sound setting is "Automatic", which means it
  follows the system *game* volume.

`PUMPKIN_SOUND` now sets the first two and `PUMPKIN_VOLUME` the third, both applied after
the stored preferences load, so an existing preferences database does not need resetting.
The game drives audio as an ARM-native `SndStreamCreate` callback at 44.1 kHz mono 16-bit.

Once it was audible it was also unlistenable, for three separate reasons:

- **The mixer was logging every instruction it executed.** `uarmInit` turned the
  interpreter's disassembler on unconditionally, and only `emupalmos_main` ever turned it
  back off. The sound callback runs on a second emulator state, which nobody had told, so
  each 44.1 kHz buffer wrote a formatted line per ARM instruction -- 1.3 GB of log in
  twenty seconds. Tracing is opt-in now (`ADISASM`), as it always meant to be.
- **The mixer ran on the ARM interpreter.** The recompiled core was wired into
  `PceNativeCall` only, and `SndStreamCreate`'s ARM callback is a separate entry point, so
  the one piece of `armc` code on a deadline was the one still being interpreted -- 28% of
  the application thread. It goes through `rarm_run` now like everything else, at 0.15%.
- **The refill was paced open-loop.** PumpkinOS asked for a fixed chunk every two thirds
  of that chunk's duration, betting that the callback costs nothing; the queue drifted
  until it either ran dry or hit the 256 KB ring and dropped a third of every chunk. It
  now refills from half of `PUMPKIN_AUDIO_MS` and asks for exactly what it takes to fill
  the rest.

The last one matters because of where the callback runs: PumpkinOS answers it on the
application's own thread, once per turn of its event loop. Between two of the game's
frames that is fine, but a level load takes a few hundred milliseconds during which no
refill is answered at all, so the queue has to be deep enough to cover one. 240 ms is;
80 ms audibly is not, and costs about 60 ms of latency less.

That is also why an unanswered refill must not be mistaken for the end of the stream.
The mixer thread waited one second for the application to answer and then reported nought
bytes -- which is how a stream says it is finished -- so the audio thread dropped it and
never asked again. One slow moment therefore silenced the game for the rest of the
session, which in a browser meant the first level load. An unanswered refill now says so
with -1 and is asked for again; only a real end of stream ends it. A queue that has run
dry is primed again before the device plays from it, rather than dribbling a chunk per
pass into one that is already playing silence.

## Display

The Palm screen is 320x320 and the window is that, scaled by an integer factor
(`BOD_ZOOM`, default 3 -> a 960x960 window). PumpkinOS's desktop is sized to match, so
there is no surrounding chrome to redraw.

The bundle sets `NSHighResolutionCapable` to **false** on purpose. With a Retina backing
store the SDL drawable is twice the window size, which PumpkinOS's `xfactor` scaling does
not account for, and the screen comes out magnified and clipped -- but only when launched
as a bundle (via Finder or `open`), because running the binary directly never reads the
Info.plist. With a 1x backing the integer scaling is exact.

## Testing

`tools/headless.sh` runs the game with no window at all, using the `libwtest` display
driver: it keeps the framebuffer in memory, writes PPM snapshots, and takes input from a
plain-text script (`tools/scripts/*.txt`). That makes it possible to drive and compare
builds without synthesising system-wide mouse and keyboard events.

    tools/headless.sh tools/scripts/ride.txt 32 build/frames

`tools/scripts/keynav.txt` is the keyboard one: it starts a level having touched nothing
but the arrow keys and Enter, so its last frame is only reached if every step of the
menus answered them. `tools/scripts/finish.txt` is its opposite: it pedals all the way
through the end of a level, where the last frame has to show the "Congratulations!"
dialog with no focus ring on it.

Useful knobs while debugging the ARM core:

| variable | meaning |
|---|---|
| `BOD_ARM_ENGINE=interp` | run all `armc` code under the interpreter |
| `BOD_ARM_BLOBS=<mask>` | bitmask of `armc` blobs to run recompiled (bit 0 = armc 1) |
| `BOD_ARM_IBLOB`/`ILO`/`IHI` | force one offset range of one blob to the interpreter |
| `BOD_ARM_BATCH=1` | exact range semantics (no overshoot) -- required for bisection |
| `BOD_ARM_RING=1` | dump recent ARM state to `/tmp/bod_ring.txt` on a wild access |
| `BOD_ARM_STRICT=1` | make out-of-range accesses fatal instead of clamped |
| `BOD_ARM_LOCKSTEP=<n>` | run every instruction of `armc` *n* under both cores and report the first register that disagrees |
| `BOD_WEB_TRACE=<n>` | report where each dispatch loop is every *n* dispatches; `1` reports every one. Written for the browser, where a blocked thread cannot be looked at, but it works anywhere |

Lockstep needs a single-step core for that blob, which is large and generated on demand:

    .venv/bin/python tools/recomparm.py --stepper 3 && make -C src -j6

`make -C src` compiles whichever `src/gen/arm<n>_step.c` happen to exist, so a plain build
has none and pays nothing for them.

`tools/bikecheck.py` scores a frame for how much of the (red) bike is visible, which is
what makes the bisection automatic.

## In a browser

The same two recompiled cores also build to WebAssembly, so the game runs in a browser
with no native binary at all:

    tools/make_web.sh                       # emcc; needs emscripten in PATH
    tools/webserver.py 8080 build/web       # then open http://127.0.0.1:8080/

`build/web` is a directory of static files (36 MB, about 12 MB over the wire) that can be
served from anywhere. It needs the two headers that make a page cross-origin isolated --
`Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`
-- because PumpkinOS runs on threads and `SharedArrayBuffer` is what they share memory
through. `tools/webserver.py` sends them; on a host that will not, the page falls back to
the bundled `coi-serviceworker.js`, which installs a service worker that adds them.

Everything the native build does, this does: the recompiled 68k and ARM cores, the
level packs, the keyboard (including the five-way navigation through menus and dialogs),
and sound. Progress, settings and best times are kept in the browser's origin private
filesystem and restored on the next visit; the page has a button that throws them away.

Two things are arranged differently from the native build, both forced by the browser:

- **The OS runs on a worker.** `-sPROXY_TO_PTHREAD` moves `main` off the browser's main
  thread, which then does nothing but answer the calls the other threads proxy to it. It
  has to stay free: every file the application opens and every SDL audio call is one of
  those, and PumpkinOS's own threads block on each other's locks, so a main thread that
  waits for a lock deadlocks the lot. That also rules out WebGL, whose context Emscripten
  can only create on the main thread, so SDL draws through its software renderer -- at
  320x320 with the page doing the scaling, that costs nothing.
- **The filesystem lives inside the wasm module** (`-sWASMFS`). The default one is
  implemented in JavaScript on the main thread, so every `open` from the application
  would be a round trip to it; the storage scan alone is a few thousand.

Safari needs one thing Chrome does not: **the audio session has to be asked for by
name.** It parks an `AudioContext` in `interrupted` -- which is neither `running` nor the
`suspended` that SDL's own autoplay recovery looks for, so neither its resume-on-gesture
handler nor its play-silence fallback ever fires, and the game plays to a device that is
not listening. The page resumes it on any gesture and whenever the tab comes back, and
keeps the listeners: the session is taken away again whenever another application wants
it. Safari also answers a refill an order of magnitude slower than Chrome does, which is
what made the silence permanent rather than a gap; see **Sound** above.

Options can be passed in the query string, so `?BOD_ARM_ENGINE=interp` or
`?PUMPKIN_USER=Me` work the way the environment variables above do, and `?BOD_DEBUG=2`
raises the log level (`2:STOR` limits the higher level to one subsystem).

`tools/webtest.js` is the browser counterpart of `tools/run.sh`: it drives the page in a
real Chrome and screenshots the canvas, with the same vocabulary of actions. Chrome is
not enough on its own -- both of the bugs above are WebKit's, and neither reproduces
there; Safari can be driven the same way over WebDriver with `safaridriver`, once
**Allow remote automation** is ticked in its Developer settings.

    tools/webtest.js -u http://127.0.0.1:8080/pumpkin.html -w 30 \
        -k "d:3,m:160/295,d:8,k:ArrowUp/down,d:4,k:ArrowUp/up" -o build/shot.png

## Licensing

PumpkinOS is GPLv3; this port and the recompiler tooling inherit that. The game itself is
not redistributable — `files/` holds the user's own copy.
