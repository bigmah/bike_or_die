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
| **↑** / **W** | Forward (pedal) |
| **↓** / **S** | Brake |
| **←** / **→**, **A** / **D** | Balance left / right |
| **Space** | **Flip** -- turn around and ride the other way |
| F1-F4 | the four Palm hardware buttons |
| **Esc** | pause: levels, packs, controls, settings (see **The pause menu**) |

Those are the game's own defaults, visible and rebindable under **Options → Control** --
in the menu bar, or under **Settings** in the pause menu. Its five actions map to a Palm 5-way
navigator: Forward=Up, Brake=Down, Balance=Left/Right, Flip=Select.

WASD rides as well, and the arrow keys still do -- the letters are added to the
navigator bits the game polls rather than translated into arrow keys, so they keep
their ordinary meaning too and typing a name or a code is unaffected. Space does the
same thing for Select.

Which letters those are is up to you. The pause menu's **Controls** page binds one key to
each of the five actions, in the window and in the browser alike; it takes effect where it
is set, and is kept -- in the app's preferences, or in that browser. Underneath, the same
thing is `BOD_KEYS`, whose default is what that page starts from, and which wins for a run
it is set for:

    BOD_KEYS="forward=w brake=s left=a right=d flip=space"

Actions are separated by spaces and the keys for one by commas, so `forward=w,i` gives
an action two keys and `flip=` takes its away. A key is a single character or one of
`space`, `enter`, `tab`, `esc`, `backspace`, `up`, `down`, `left`, `right`, `pgup`,
`pgdown`, `home`, `end`, `ins`, `del`, `f1`-`f12`. The arrow keys ride whatever this
says: they are the Palm navigator itself rather than an extra binding, so the bike can
always be steered with them.

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

The game's own menus and dialogs are not drawn any more -- see **The game's own menus and
dialogs** below -- so this is what `BOD_NATIVE_UI=0` goes back to, and what the port had
before the host drew them. Every menu and dialog is fully navigable without the mouse,
which still works exactly as it did:

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

## Levels and level packs

The window's title bar carries the controls the browser build has above its screen:
**Level Pack…**, **Levels…**, Previous / Restart / Next, with the name of the pack the game
is on between them, and **Pause**. The same commands are in the menu bar under **Level**:

| key | action |
|---|---|
| **⌘L** | Level Pack… |
| **⇧⌘L** | Levels… (the game's own level list) |
| **⌘[** / **⌘R** / **⌘]** | Previous / Restart / Next level |

**Level Pack…** brings a sheet down over the screen that lists every pack the game has, in
the game's order, with a field to find one. Return or a double-click starts the selected
pack; typing selects the first that matches. Escape closes the sheet and the game's
dialogs behind it. Like the browser's panel it is a front for the game's own chooser,
driven through `bodpack.c` (see **In a browser**), so it does exactly what Game -> Select
Level -> More... does.

The controls are Cocoa, in `src/liblsdl2/liblsdl2_mac.m`, added to SDL's window when the
display driver creates it. Two things are arranged around the game:

- **What is typed into the sheet stays out of the game.** SDL reports every key the
  application receives, whichever window has it, so a pack name typed into the search field
  would also reach the game's pack list behind the sheet, and Return would press its
  Select. While the sheet is up, and for any Command chord, the display driver keeps a key
  from the game from its press until its release -- the release is what types a character
  in PumpkinOS, so both halves have to go. A key that was already down for the game when the
  sheet came up is still let go of there.
- **The window stays the size it was.** The controls sit in the title bar rather than in a
  strip under it: at the default zoom the window already fills a laptop's screen above the
  Dock, and a strip would push the bottom of the game under it. The title, which only ever
  said "PumpkinOS", makes way.

Picking a pack used to leave the game on the one it was already on, in both builds. The
chooser did its part -- the level list came back titled with the new pack, and the pack's
results database was created -- but the level that started was Introduction's, and so was
every level after it. The game finds the pack it has switched to by name, from its ARM
code, through Palm OS 5's `DmFindDatabase` (offset 0x15C of the Boot library's table),
which PumpkinOS did not provide. A call it does not provide hands back its first argument
untouched, so the game opened the pointer to the pack's name as if it were the pack, was
refused, and quietly fell back to "BOD - Introduction". `armsyscall.c` answers it now.

## The pause menu

**Escape pauses**, in the window and on the page. So do **Pause** -- in the title bar, under
**Level**, above the screen in a browser -- and minimizing the window, hiding the app or
putting the tab away. The game really stops: the runtime holds its thread and leaves the
time out of its clock (see **In a browser**, where this was first needed), so it carries on
from the moment it stopped. A menu comes down over it with everything that is not riding:

| | |
|---|---|
| **Resume** | or Escape again |
| **Restart / Next / Previous level** | |
| **Choose a level…** | the game's own level list |
| **Level packs…** | the pack sheet or panel |
| **Controls** | the key each riding action answers to (see **Controls**), and the game's own control options |
| **Settings** | Screen (see **Display**), Touch controls in a browser, and the game's Options menu |
| **Profiles & records** | the rest of the game's Game and Rec menus: profiles, statistics, recorded games |
| **Help** | the game's Help menu |

Up and down move, left and right change a row of choices, Return or Space picks, and
Escape goes back a page and then resumes. A new pause starts on Resume, so Escape twice is
always there and back. The game's dialogs open from it with the game still held, and a
dialog that is dismissed -- Escape, Cancel, OK, Close, Done -- comes back to the menu where
it was; one left some other way, Play this level or a replay, ends the pause, since that is
what was asked for. The pack sheet is the same: back to the menu without a choice, riding
with one.

In the window the menu is a sheet (`src/liblsdl2/liblsdl2_mac_pause.m`), and a window has
one sheet at a time, so it steps aside for a dialog's sheet or the pack sheet and comes back
after. Its rows are its own rather than buttons: the system only lets the keyboard move
between buttons when Keyboard navigation is turned on in System Settings, and then only by
Tab. The same goes for **the game's dialogs in the window, which are now walked by the
arrow keys too** (`liblsdl2_mac_keys.m`): the sheet hands every key to its owner before
any control sees it, the arrows move to the control that is that way on the sheet, left and
right change a pop-up, a segmented control or a slider, Return or Space presses, and a
ring drawn just outside the control says where the keyboard is. A list keeps up and down
until they would leave it; Return in a list whose default button is Cancel -- the level
list -- chooses the item and moves on to Play this level, and a double-click plays it,
where it used to choose it and press Cancel.

Two things had to be arranged for the window that the page does not need. SDL takes a key
before the sheet it was typed into does, and hands it to the game's side later, when the
sheet may be gone: Escape on the menu resumes, and the same Escape would then pause again.
So each sheet notes when it comes down, and a key whose SDL event is no later than that is
the window's. And the game publishes a dialog again every time something on it is chosen,
which reloaded the sheet's lists and let go of their selection, so a choice lasted only
until the game had heard it; the selection is put back now.

## The game's own menus and dialogs

Everything around the riding was Palm OS 5's own UI: a menu bar behind the menu key,
twenty-five forms, and the alerts the game puts up as it goes. On a Palm that was the
machine's own look. In a window on a Mac, or on a page, it is a 320x320 postage stamp of
somebody else's operating system blown up by three, and none of it can be driven the way
the rest of the machine is.

So none of it is drawn. The forms are still there and the game still runs them -- they
are what knows what a level pack is, what the sound settings mean, which recording is
which -- but their windows never reach the screen, and what is on them is published for
the host, which puts up controls of its own. A press on one of those goes back into the
form the way the pen would. The macOS window draws them as the game's four menus in the
menu bar and a sheet over the game; the page draws them in its pause menu and a panel over
the screen (see **In a browser**). The menu key opens nothing now: what was behind it is
in the menu bar.
`BOD_NATIVE_UI=0` puts the game's own back, drawn where they always were.

Three things make that possible, and all three were already true:

- **Every form draws into a bitmap of its own** (`FrmInitFormInternal`), and reaches the
  screen only by being the active window when something is drawn -- one place,
  `WinDirtyRegion`. Dropping a hidden form's updates there leaves the game's last frame
  on the screen, untouched, with the form live behind it. The one part of a form that
  goes straight into the display rather than into the form's own window is a dialog's
  border, which would otherwise draw a frame around nothing (`FrmDrawEmptyDialog`).
- **A form works with nobody looking.** Its handlers run on the game's own thread out of
  its own event queue, so a control pressed from the host is indistinguishable from one
  pressed by the pen.
- **The host was already talking to that side**, to drive the level pack chooser
  (`bodpack.c`). This is the same arrangement, generalised: a command string in, a
  description of what the game has on screen out, both across a sequence number so that
  neither thread has to take a lock (`src/libpumpkin/bodui.c`).

What is published is the form and its objects -- kind, id, label, value, the push button
group, the list behind a popup trigger, and where each one sits on the form -- with a
list's items behind it. The items are read the only way they can be: the game draws its
own list rows, a level's name and its best time made up as the row is drawn, so each is
drawn once into an offscreen window with `WinDrawChars` watched. The menu bar is
published once, out of the game's own `MBAR`, and an item is named by where it is rather
than by what it says: the game has two menus called "Control" and three called "Hall of
Fame". Text is UTF-8, converted from the game's own character set, which is Latin-1 with
Windows' punctuation in the middle -- the ellipsis of "About..." is a single byte there.

**One renderer, not twenty-five panels.** Nothing on either host knows what any
particular form means. A Palm form is laid out in absolute coordinates, so the objects
that shared a line on its screen are the ones that belong together, and that is how the
rows are found -- by the geometry the game already has, rather than by a table of forms
kept in step with it by hand. Buttons become buttons, a group of push buttons becomes one
segmented control, a popup trigger and its list become a pop-up button, a list becomes a
list, a field that the game only writes into becomes text rather than somewhere to type,
and a dialog's bottom row of buttons becomes the row under the sheet where a Mac keeps
them. A form the game relabels, or one nobody thought about, comes out as controls
without anything being added. Objects parked off the side of the form are left there:
that is where a Palm dialog keeps the page it is not showing.

**The dialogs the game puts up by itself** -- finishing a level asks for a name, and a
level's statistics are an alert -- arrive the same way, so they are drawn the same way. A
dialog has to stand still for 450ms before it is shown, though: some of the game's own
are answered on the way in and gone again in a fraction of a second, the registration
dialog among them (see **Registration**), and a sheet that appeared for those would be a
dialog nobody asked for over a game that had not started.

**A dialog that waits forever.** `FrmDoDialog` runs its own event loop, and with nothing
on its way it parks the game's thread inside `EvtPumpEvents` until something arrives from
the window. Nothing does: the dialog is on the host's screen now, and the answer comes
from another thread. That wait takes the host's commands too, and anything they put in
the queue ends it -- otherwise the first alert of a session is a dialog nobody can reach.

**The level pack sheet stays what it was.** It drives the game's own level and pack lists
from the outside (`bodpack.c`), and those dialogs are hidden like every other; while it
is doing that they are its business, and the generic sheet leaves them alone.

**A press is a pen's press.** A pen changes a checkbox or a push button as it goes down on
it, and the game reads the new value from the control when it hears the `ctlSelectEvent`
that follows. `CtlHitControl` is only the second half, so a checkbox pressed from the host
used to stay as it was -- and the value published was the control's highlight while the
pen is on it rather than whether it is on, so no checkbox ever showed ticked and no tab
showed chosen either. Both are what the pen does now.

**Commands go one at a time, or together.** The runtime keeps one command, and a second
sent before the first was taken replaces it. A host that means two things at once --
choose this item, then press Select -- sends them as one, a line each, and they are carried
out in order; one that sends them apart waits until `bod_ui_taken()` says the first has
been taken. The page used to send those two back to back, and the choice was usually lost.

Two differences worth knowing. The sliders on Sound Options come through as plain buttons
in the browser and as sliders natively -- a control's style reads differently in
PumpkinOS's 32-bit build, which is what the WebAssembly one is. And `tools/scripts/*.txt`
that expect to see a Palm dialog on the screen, `finish.txt` among them, want
`BOD_NATIVE_UI=0`; `tools/scripts/dialogs.txt` is the one that checks the new arrangement,
opening every dialog the menu bar can and reading each back.

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
| `BOD_NATIVE_UI` | `1` | the host draws the game's menus and dialogs; `0` lets the game draw its own |
| `BOD_SCREEN` | View menu | how the screen is upscaled: `pixels`, `smooth` or `xbr`; unset, the last choice in the View menu, which starts as `xbr`. See **Display** |
| `BOD_KEYS` | Controls page | the keys that ride, on top of the arrows; unset, what the pause menu's Controls page last chose, which starts as WASD. See **Controls** |
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
before the dialog is ever seen.

Answering it leaves it up: the same form redraws as its "registered to"
thank-you, which somebody then has to close before the game starts. So that
button is pressed too, on the redraw, and the game comes up in play rather than
behind a dialog nobody asked for. Only that one redraw is touched -- the About
dialog opened from the menu later on behaves normally, and a code the game
refuses leaves "Unlock" where it was, which is the signal to stop and leave the
dialog alone. `BOD_UNLOCK=0` leaves it in trial mode; anyone whose code was
issued for a different HotSync name sets both variables.

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
there is no surrounding chrome to redraw. On a Retina display the window has twice that
many pixels -- 1920x1920, six to each of the game's -- and the game is drawn into all of
them.

How the game's 320x320 is made up to those is the **Screen** setting: **View** in the
menu bar (⌘1, ⌘2, ⌘3), and **Settings** in the pause menu in a browser. It is kept, in the user defaults
natively and in the browser's storage on a page; `BOD_SCREEN` (or `?BOD_SCREEN=`) says
it for one run without keeping it.

| | |
|---|---|
| **Pixels** | every one of the game's pixels a square, as a Palm showed it |
| **Smooth** | bilinear |
| **xBR** | the default. Hyllian's xBR (level 2): each pixel's corners are redrawn from the edges that run through its 5x5 neighbourhood, so lettering, wheels and the outlines of hills come out as smooth lines at the display's own resolution, while flat colour and texture stay as they were |

**In a browser** each is a fragment shader (`src/emscripten/bod-pre.js`). The canvas's
backing store is its CSS size times `devicePixelRatio` -- one of its pixels per device
pixel -- and the frame goes up as a 320x320 texture, so a bigger screen costs the GPU and
not the page: riding measures 58.0 frames a second with xBR against 58.7 with pixels.
Pixels there is not quite nearest-neighbour. At a whole-number scale it is exactly that,
but a phone's screen is as big as the phone allows, and at 3.125 device pixels to a game
pixel the squares cannot all be the same size; the one device pixel that straddles each
seam is blended instead of some rows coming out doubled. Without WebGL the canvas stays
320x320 in 2D, the browser does the scaling, and xBR is not offered.

**Natively** SDL's renderer has no way to run a shader, and it draws each window's
texture straight into its back buffer, rectangle by rectangle, so there was never a picture
of the whole screen to upscale either. So the frame is composited on the CPU, the way the
browser build already had to (`src/liblsdl2/liblsdl2_screen.c`), and each render upscales
it to the window's pixels and draws it as one texture. xBR is the shader's arithmetic in C
and the same picture to the last bit -- checked against Chrome's output at 2x, 4x and 6x.
Most of any frame has no edge near it and is a copy, so a whole frame at 6x, 3.7 million
pixels, takes about a millisecond on one core. Spread over all of them it took a quarter of
that but a third more CPU in all, and in the running game the threads waking for every frame
cost more than half a core, so it runs on the main thread, where the frame is. Smooth and pixels
at an exact multiple are the renderer's own scaling; pixels at any other size -- full
screen, say -- are made up to the whole multiple below it and blended the rest of the way,
as in a browser.

The bundle sets `NSHighResolutionCapable`, and the window asks SDL for a high-density
drawable. It used to do neither on purpose: with a Retina backing store the drawable is
twice the window's size, which PumpkinOS's `xfactor` scaling did not account for, and the
screen came out magnified and clipped. Nothing depends on that any more -- the whole frame
is drawn into the renderer's logical size in one piece, and the texture it is drawn from is
sized from `SDL_GetRendererOutputSize` -- and a window moved to a display of a different
density is drawn again at the new size straight away.

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
dialog with no focus ring on it -- both want `BOD_NATIVE_UI=0`, since the dialog they
are looking at is one the host draws now. `tools/scripts/keys.txt` rides with the letters
rather than the arrows, so running it under a `BOD_KEYS` that moves them elsewhere
should leave the bike where it stands. `tools/scripts/packs.txt` switches level packs
through `bodpack.c` -- a script's `pack` action sends it a command, as the window's
level controls do -- and logs the chooser's status before each one, including the title
of the level list the game last showed: after the switch that has to be the new pack's
name, not "BOD - Introduction". `tools/scripts/dialogs.txt` opens every dialog the game's
menu bar can and reads each one back -- a script's `ui` action drives `bodui.c` the way
`pack` drives `bodpack.c`, and `ui dump` writes what the game has on screen into the log
-- so a form that comes up empty, or takes the game with it, shows up there; the frames
it takes have to be of the game throughout, since none of those dialogs is drawn. A `ui`
line can carry several commands separated by `;` (`ui press 10; press 12`), which go to
the game as one, and `ui pause 1` / `ui pause 0` hold the game still and let it go --
`tools/scripts/pause.txt` rides into a pause with Up held and has to come out of it with
the bike where it stopped.

`tools/macsheets.sh` tests the window's sheets -- the pause menu, the game's dialogs, the
level pack sheet -- without showing anything on a screen: it compiles
`liblsdl2_mac*.m` against a simulated game, keeps a window that refuses to be on any
display, never lets itself become the active application, and hands each key press to the
sheet as an event. It walks the pause menu, binds a key, changes the screen, opens Sound
and Display and comes back, ticks a checkbox, chooses and plays a level, backs out of the
pack sheet, and checks that the Escape which resumes is not taken for a new pause. The
app's preferences are left alone: the suite the sheets keep things in is swapped for a
throwaway one.

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
| `BOD_FPS=<n>` | how often at most the screen is copied to the display, in frames a second (default 60, clamped to 5-240). `20` is what PumpkinOS does on its own; lower is cheaper on a phone's battery. In a browser the display's own refresh paces the frames and this is the ceiling on it |
| `BOD_PRESENT=sdl` | in a browser, hand the frame to the canvas through SDL again rather than from the page. Slower, and there for comparing the two |

Lockstep needs a single-step core for that blob, which is large and generated on demand:

    .venv/bin/python tools/recomparm.py --stepper 3 && make -C src -j6

`make -C src` compiles whichever `src/gen/arm<n>_step.c` happen to exist, so a plain build
has none and pays nothing for them.

`tools/bikecheck.py` scores a frame for how much of the (red) bike is visible, which is
what makes the bisection automatic.

For the browser build, `tools/webtest.js` drives it in a real Chrome -- booting it, sending
keys and clicks, and taking screenshots -- and `tools/webfps.js` measures how fast it is
actually drawing while being ridden, reporting the rate and the spread of the gaps between
frames. Both need the local Chrome and puppeteer-core that `tools/setup.sh` installs.

    tools/webserver.py 8080 build/web &
    tools/webfps.js -u http://127.0.0.1:8080/pumpkin.html

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
level packs, the game's menus and dialogs drawn by the page rather than by the game, the
keyboard, and sound. Progress, settings and best times are kept in the browser's origin private
filesystem and restored on the next visit; `bodReset()` from the console throws them away.

**Escape pauses**, and the pause menu comes up over the screen; see **The pause menu**.
Quit is not in it -- in a page it ends the game for good -- and nor is sending the game to
another Palm. The keys set on the Controls page go to the runtime through
`pumpkin_set_ride_keys`, and they and the Screen and Touch settings go into this browser's
storage, so they take effect at once and are there on the next visit.

It really stops the game. The page's Pause used to stop the loop that draws the screen,
and the picture stood still while the game carried on under it: five seconds "paused" with
Up held rode the tutorial to the red flag and crashed, the same as five seconds riding.
Pausing now asks the runtime to hold the game (`pause 1`, `src/libpumpkin/bodui.c`). The
game's own thread waits in `EvtGetEvent` before it is handed anything, and the game's clock
leaves that time out (`TimSkipTicks`): Bike or Die times its riding off `TimGetTicks`, so a
pause that let the ticks run on would give it the whole pause as one long frame. It carries
on from the moment it stopped -- eleven seconds held measures the same as none. Held is not
deaf: the thread goes on answering the window and the mixer -- the mixer with silence -- and
taking the host's commands, which is how the menu opens the game's dialogs and switches
levels from behind the pause. A dialog of the game's is never held, since it has stopped the
game already. The audio context is suspended as well, which gives up the audio session.

**All of it is driven by the arrow keys**, and nothing in any panel needs the mouse: the
pause menu, the level packs, and every dialog of the game's the page draws. The arrows move
to the control that is that way on the screen -- down a list, across a row of buttons, from
the last row round to the first -- Enter or Space presses it, Escape goes back a step, and
Tab goes round inside the panel. Where the controls are is where they are drawn, so a dialog
nobody laid out by hand is walked the same way. A few keep an arrow for themselves: a text
field keeps left and right for its caret, a drop-down takes them as the previous and next
choice, and a row of choices -- Screen, Touch controls -- picks the next one as the arrow
reaches it. A dialog opens with the keyboard on the chosen item of its list, somewhere to
type, or its first control. Choosing a level in the game's level list and pressing Enter
chooses it and moves on to Play this level: that list's default button is Cancel, and the
page used to press it on every click, throwing the choice away.

**Level pack** and the level buttons above the screen do what the game's Game menu does,
without the menu, and end a pause if there is one. Restart, Previous and Next each queue the matching menu event, the way
picking the item with the keyboard would. **Levels...** opens the game's own level list.
**Level pack** opens a panel over the screen that lists every pack the game has, in the
game's order, with a filter; picking one starts it. The panel is a front for the game's
own chooser, not a replacement: the runtime walks Game -> Select Level -> More... exactly
as a pen would, reads the names off that dialog's list -- the list draws its items through
a callback in the game's code, so each item is drawn once into an offscreen window with
`WinDrawChars` watched -- and, when the page hands a choice back, sets that list's
selection, presses Select, and presses "Play this level" on the level list that comes
back (`src/libpumpkin/bodpack.c`, driven through `bod_pack_command` and read back through
`bod_pack_state`). The game is always the one that switches, so its results database,
best times and everything else follow. Closing the panel presses Cancel on both dialogs.

Two things in the browser stood between a click and a pack until this worked. A quick
click delivers its press and release together, and the game's thread could pump the
release before its own handler had finished with the press -- so the release found no
object under it, the list drew the new row highlighted but never took the selection, and
Select chose the old pack. A release that arrives while its press is still in flight now
waits, the way a keyboard tap's already did, until the queue has drained past the press
(`FrmDeferPenUp`). The second was a hang: switching to a pack for the first time creates
its results database, and opening that reads its empty index file -- and WasmFS answers
`poll()` for a regular file by whether it has any bytes at all, never blocking, so
`select()` said "not ready" at once and forever, and `sys_read()` asked forever. A regular
file is always ready; the browser build no longer asks (`sys_select`).

The pause menu's game items hand the game the menu event it would have got from its own
menu, and whatever that opens is drawn by the page too, as a panel of real controls built
out of what the form publishes; see **The game's own menus and dialogs**. Escape over that
panel does what its Cancel would. The dialogs the game raises by itself, finishing a level
among them, come up the same way.

**Sixty frames a second**, arriving one per display refresh. The ride that measured 16.3
frames a second when this port was first playable, and 37.5 once the refresh cap came off,
now measures 59.1 -- and the gap between one frame and the next is 16.7ms at the median
and 17.3ms at the ninetieth percentile, which is the part that reads as smooth.
`tools/webfps.js` is what measures it: it boots the game, rides it, and counts the frames
where they land, by wrapping the page's 2D and WebGL contexts before anything loads, so it
measures whatever the build does rather than what the build says it does.

Three things stood between the game and the display, and all three had to go.

**The present blocked the game.** SDL's software renderer presents with
`MAIN_THREAD_EM_ASM`: the thread holding the frame stops while the browser's main thread
converts 102,400 pixels in JavaScript and hands them to `putImageData`. That is a
synchronous hop to another thread once a frame and costs about 8ms. So the frame does not
go through SDL at all now (`src/liblsdl2/liblsdl2_web.c`). The game converts it to RGBA
where it already is -- in wasm, off a 64K lookup table, a few hundred microseconds -- into
a buffer in the shared heap, and bumps a counter; the page reads that buffer in its own
`requestAnimationFrame` and puts it on the canvas. Neither side ever waits for the other.
A counter either side of the copy is what keeps a frame published mid-copy from being torn
into the one before it. `BOD_PRESENT=sdl` puts it back the old way, which is how the two
are compared.

**Sixty turns a second was beating against sixty refreshes a second.** The loop that
copies the screen to the display is Emscripten's `MainLoop`, and asking it for
`requestAnimationFrame` on a worker -- where there is none -- gets a `setTimeout` aimed at
sixty a second. Free-running against a display refreshing sixty times a second, a turn
that lands a hair early finds the 16,666-microsecond gate shut, and the frame waits for
the next turn: 33ms, every other frame, which is exactly the 37.5 above. The loop now
turns several times per refresh (`LOOP_HZ` in `src/libos/libos.c`), which also lifts the
ceiling on how fast the keyboard and the pen are read, since the loop takes one input
event per turn.

**And the pacing is the display's.** A window provider can now answer `refresh_due`,
saying whether the display wants a frame yet; the browser's does, from the refreshes the
page counts, so a frame is drawn just after a refresh and shown on the next one -- evenly
spaced, rather than on a clock of our own. `BOD_FPS` is passed down as the shortest gap to
allow, so it stays the ceiling it always was: 30 gives 30.0 frames a second with 33.3ms
between them, and on a display that refreshes faster than 60 it is what holds the game to
60. If the page stops refreshing at all -- a tab in the background -- the counter stops,
and after a quarter of a second the game goes back to its own timer, so nothing depends on
the page being there. Every other display answers -1 and nothing changes for it.

It still costs nothing when the game is not drawing: the copy only happens if `draw_task`
finds a dirty rectangle -- `fullrefresh` is off in this build -- so a refresh that finds
nothing new spends nothing. And none of it makes the bike go faster: three seconds of
pedalling covers the same ground as before, because the physics runs off the clock rather
than off the frame. What the shutter was costing is smoothness, and the delay before a key
shows on screen, which between them are most of what "slow" means in a game.

One thing the old path was doing for free had to be done by hand. `SDL_RenderCopy` clips
against the render target, so the rectangles the window manager hands down have never had
to be inside it -- and it hands down four, the borders it draws around a task window, at
(-4,-4) and (320,-4). Blitted unclipped they run off the end of the frame buffer and into
whatever follows it, which was the colour table: every dark colour in the game came out
wrong, so the grass was periwinkle and the tyres were tan while the sky, higher up the
table, was perfect. Both the blit and the texture upload clip now.

The emulation is not what is in the way, which is worth saying because it is the thing
that looks expensive. Counted where the game marks its own screen dirty, it draws about
five hundred frames a second; all that was ever throttled is how many of them were carried
to the display. Throttling the machine to a sixth of its speed -- enough to stretch the
boot from 1.2s to 6.0s -- left both measurements roughly where they were. Recompiling the
cores at `-O2` would be optimising something with at least six times the headroom it
needs, on functions the note above says clang already handles badly.

**Touch controls** are off unless they are asked for: **Settings → Touch controls** in the
pause menu, which is kept in this browser, or `?touch=1` and `?touch=0` for one visit. They
used to come on by themselves for any coarse pointer; now a phone gets the same page as
everything else until they are turned on, and they come and go without a reload, since
the game would go with it. With them on, the page stops being a page with a game on it and
becomes the game. It rearranges: the heading goes and
its buttons join the level buttons on one line at the top, the key legend goes with it,
the status line moves over the screen, and the screen takes everything that is left.
Nothing scrolls, and the safe-area insets keep it clear of a notch and a home indicator.

The keys the game is ridden with come with it, for a phone or a tablet with no keyboard to
press them on: the four arrows in a cross and the space bar beside them, under the screen when the
phone is upright and down either side of it when it is not. They send the keystrokes a
thumb is asking for rather than talking to the game -- SDL listens for `keydown` and
`keyup` on the window, so a `KeyboardEvent` dispatched there is the same press, with the
same `code` and `key` a real one carries, and whatever Controls has bound follows along.
The arrows always ride, because the runtime wires them to the navigator itself; space
rides whatever the Controls page says it does, which is Flip until someone says
otherwise. Each key is held for as long as the thumb is on it, which is what riding
wants -- the game reads the keys as a mask rather than as repeats -- and each takes one
pointer and captures it, so two can be held at once and a thumb that slides off a key
still lets go of it. A key held when the tab goes away is released. While a panel is open
the pad is dimmed and inert, the panel having taken the keys for itself, and the pad's
keystrokes are marked so that a thumb cannot be the answer to the Controls page's
"press a key".

The screen is a square and a phone is not, so the square and the keys are sized together
(`bodFit`) rather than by the stylesheet: each depends on the other. Upright, the cross is
three keys tall under the screen, so every pixel a key gains costs the square three; on
its side the cross and the space bar are five keys wide beside it, and it costs five. The
keys take as much of the room beside the square as they can use and the square takes the
rest, which on an upright phone is the whole width of it. The page a desktop gets is
untouched by any of this: the grid that arranges the three of them is `display: contents`
until the touch layout is on.

Two things are arranged differently from the native build, both forced by the browser:

- **The OS runs on a worker.** `-sPROXY_TO_PTHREAD` moves `main` off the browser's main
  thread, which then does nothing but answer the calls the other threads proxy to it. It
  has to stay free: every file the application opens and every SDL audio call is one of
  those, and PumpkinOS's own threads block on each other's locks, so a main thread that
  waits for a lock deadlocks the lot. That also rules out WebGL for SDL, whose context
  Emscripten can only create on the main thread, so SDL draws through its software
  renderer at 320x320 -- and the page, which is on the main thread, draws the frame through
  WebGL itself (see **Display**).
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

`--dpr 2` runs the page at a Retina display's density, which is what the screen is
upscaled to, and takes the screenshots at it: `j:Module.bodScreen.set('pixels')` between
two `p:` actions is the same moment with and without xBR.

`j:` runs a line of JavaScript in the page between the other actions -- `j:bodPack.open()`
opens the pack panel, `j:bodPack.pick(1)` takes its second entry, `j:bodPM.open()` pauses --
which is how the buttons are driven from a script. It cannot contain a comma. `k:Escape`,
`k:ArrowDown` and `k:Enter` drive the pause menu the way a player does.

## Licensing

PumpkinOS is GPLv3; this port and the recompiler tooling inherit that. The game itself is
not redistributable — `files/` holds the user's own copy.
