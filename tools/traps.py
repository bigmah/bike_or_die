import re
H='vendor/palm-os-sdk/sdk-5r4/include/Core/CoreTraps.h'
NAME={}
for line in open(H,encoding='latin1'):
    m=re.match(r'\s*#define\s+(sysTrap\w+)\s+(0x[0-9A-Fa-f]{4})\b',line)
    if m:
        v=int(m.group(2),16)
        NAME.setdefault(v,m.group(1)[7:])
def name(t): return NAME.get(t,'?')
