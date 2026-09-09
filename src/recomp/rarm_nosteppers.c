/* The single-step ARM cores exist only for BOD_ARM_LOCKSTEP, which diffs the
 * recompiled code against the interpreter while debugging a translation bug.
 * They are ~10 MB of generated C, so the wasm build leaves them out and the
 * lockstep checker simply finds no stepper. */
#include "rarm.h"

rarm_step_fn rarm_stepper(int n) { (void)n; return 0; }
