#!/usr/bin/env python3
"""Write the Nintendo logo and the header complement into a GBA ROM image,
and pad it to a multiple of 256 KB. Same job as devkitPro's gbafix."""
import sys
LOGO = bytes.fromhex(
    '24ffae51699aa221 3d84820a84e409ad 11248b98c0817f21 a352be199309ce20'
    '10464a4af82731ec 58c7e83382e3cebf 85f4df94ce4b09c1 94568ac01372a7fc'
    '9f844d73a3ca9a61 5897a327fc039876 231dc76103 04ae56 bf388400 40a70efd'
    'ff52fe036f9530f1 97fbc08560d680 25 a963be03014e38e2 f9a234ffbb3e0344'
    '7800 90cb88113a94 65c07c6387f03caf d625e48b380aac72 21d4f807'.replace(' ', ''))
assert len(LOGO) == 156, len(LOGO)

def fix(path):
    rom = bytearray(open(path, 'rb').read())
    rom[4:4 + 156] = LOGO
    chk = 0
    for b in rom[0xA0:0xBD]:
        chk = (chk - b) & 0xFF
    rom[0xBD] = (chk - 0x19) & 0xFF
    pad = (-len(rom)) % (256 * 1024)
    rom += b'\xff' * pad
    open(path, 'wb').write(rom)
    print(f'{path}: {len(rom)} bytes ({len(rom) / 1048576:.1f} MB)')

if __name__ == '__main__':
    for p in sys.argv[1:]:
        fix(p)
