# Changes to PumpkinOS

`vendor/PumpkinOS` is upstream PumpkinOS plus the changes below. `git -C vendor/PumpkinOS diff`
shows them all; nothing else in the tree is modified.

## macOS / arm64 port

| file | change |
|---|---|
| `src/common.mak` | `arm64` machine branch (64-bit, no `-m32`) and a `Darwin` OS branch: `SOEXT=.dylib`, clang, `-DDARWIN` |
| `src/Makefile` | Darwin module list (`liblsdl2` + the `linux` main.c), and a bounded `-j $(JOBS)` — the unbounded `make -j` forks one clang per source file and thrashes |
| `src/liblsdl2/Makefile` | Darwin branch: include/lib paths from `sdl2-config`, `-framework OpenGL -framework Cocoa` instead of `-lGL` |
| `src/libpit/sys.c` | Darwin headers (`sys/mount.h` for `statfs`), `pthread_threadid_np` for `gettid`, one-argument `pthread_setname_np`; force `_POSIX_TIMERS` on (macOS advertises `-1` but `clock_gettime` and the MONOTONIC_RAW / *_CPUTIME_ID clocks all work); use `SOEXT` instead of a hardcoded `".so"` when `dlopen`ing plugins |
| `src/libpit/mutex.c` | `sem_timedwait` does not exist on macOS — poll `sem_trywait` to the deadline |

## Host configuration hooks

| file | change |
|---|---|
| `src/libpumpkin/DLServer.c` | `DlkGetSyncInfo` returns `$PUMPKIN_USER` when set. Bike or Die keys its registration code to the HotSync user name, so it has to be settable. |
| `src/libpumpkin/pumpkin.c` | `PUMPKIN_DISPLAY_LE` / `PUMPKIN_SOUND` override the per-app registry entries. The game's ARM blitter writes little-endian RGB565; without the first one every colour is wrong. |

## Static recompilation bridge

All in `src/libpumpkin/emulation/emupalmos.c` (plus a trace hook in `arm/armemu.c` and a
frozen clock in `armsyscall.c` used only while diffing):

- `r68k_trap` publishes the recompiled 68k register file into the Musashi context, calls
  the existing `palmos_systrap`, and reads the results back — so the whole trap layer is
  reused unchanged.
- `r68k_resolve` / `rarm_resolve` find the runtime addresses of the `code` and `armc`
  resources. Code resources 2+ are loaded by the app itself, so they resolve lazily.
- `r68k_interp` falls back to Musashi for 68k that is not ours (PumpkinOS synthesises a
  few thunks), running until control returns to a recompiled segment.
- `rarm_escape` handles the three ways ARM leaves the blobs: the return stub, the
  call-68K trampoline, and the PACE syscall addresses (`0x04G0NNNN`).
- `rarm_badaddr` bounds-checks recompiled ARM memory accesses and clamps wild ones
  (`BOD_ARM_STRICT=1` makes them fatal instead).
- Tracing (`BOD_ARM_TRACE`, `BOD_ARM_ENGINE`, `BOD_ARM_DETTIME`, `BOD_ARM_RING`) exists to
  diff the recompiled ARM against the interpreter instruction by instruction; that is how
  the two real translation bugs were found. See `tools/diff_trace.py`.
