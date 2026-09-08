"""Find the first instruction where the recompiled ARM diverges from the interpreter."""
import sys
def load(path):
    # The interpreter also traces the magic PACE syscall addresses; the
    # recompiled core handles those outside the traced code, so drop them.
    out = []
    for ln in open(path).read().split('\n'):
        p = ln.split()
        if not p: continue
        if int(p[0], 16) >= 0x04000000: continue
        out.append(ln)
    return out
A = load('/tmp/bod_trace_interp.txt')
B = load('/tmp/bod_trace_recomp.txt')
print(f"interp {len(A)} lines, recomp {len(B)} lines")
NAMES = ['pc','r0','r1','r2','r3','r4','r5','r6','r7','r8','r9','r10','r11','r12','sp','lr']
for k in range(min(len(A), len(B))):
    a, b = A[k].split(), B[k].split()
    if not a or not b: break
    # compare pc + r0..lr (flags are not available from the interpreter hook)
    if a[:16] != b[:16]:
        print(f"first divergence at trace index {k}")
        lo = max(0, k-6)
        for j in range(lo, min(k+3, len(A), len(B))):
            aa, bb = A[j].split(), B[j].split()
            mark = ' <<<' if j == k else ''
            print(f"  [{j}] pc={aa[0]}  interp {' '.join(aa[1:16])}{mark}")
            print(f"       pc={bb[0]}  recomp {' '.join(bb[1:16])}")
            if j == k:
                for i in range(16):
                    if aa[i] != bb[i]:
                        print(f"       -> {NAMES[i]}: interp {aa[i]} vs recomp {bb[i]}")
        sys.exit(0)
print("no register divergence in the common prefix")
