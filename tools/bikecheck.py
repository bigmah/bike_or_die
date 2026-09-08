"""Score how much of the bike is on screen: the model is strongly red."""
import sys, struct
def load(path):
    d = open(path,'rb').read()
    # P6\n<w> <h>\n255\n
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]
def redscore(path):
    w, h, px = load(path)
    n = 0
    for i in range(0, w*h*3, 3):
        r, g, b = px[i], px[i+1], px[i+2]
        if r > 120 and r > g + 60 and r > b + 60:
            n += 1
    return n
for p in sys.argv[1:]:
    try: print(f"{redscore(p):6d}  {p}")
    except Exception as e: print(f"   ERR  {p} {e}")
