#!/usr/bin/env python3
"""Disambiguate FeatureSet reply layout via Root.GetFeature existence checks."""
import sys, time
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, 'tools'))
from probe2 import Client, hx, RID_SHORT, RID_LONG

c = Client('/dev/hidraw8')

CANDIDATES = [
    # (guess_A_fid from [idx][fidHi][fidLo] reading, guess_B_fid from [fidHi=P0][fidLo=P1] reading)
    (0x0100, 0x0001), (0x0300, 0x0003), (0x0500, 0x0005), (0x4b00, 0x1d4b),
    (0x2000, 0x0020), (0x0400, 0x1004), (0x5000, 0x2250), (0x5100, 0x2251),
    (0x0200, 0x2202), (0x9000, 0x8090), (0xe000, 0x80e0), (0x0c00, 0x1b0c),
    (0x6100, 0x8061), (0x0000, 0x8100), (0x1000, 0x8110), (0x0000, 0x1500),
]

print('Root.GetFeature existence probe (fid exists => nonzero idx returned;')
print('missing => error 0x02 in params or no valid idx):')
print(f"{'guess A':>9s} {'guess B':>9s} | resultA          | resultB")
for fa, fb in CANDIDATES:
    ra = c.root_get_feature(fa)
    rb = c.root_get_feature(fb)
    fa_str = f'idx={ra[4]:02x} t={ra[5]:02x} v={ra[6]:02x}' if ra else '-'
    fb_str = f'idx={rb[4]:02x} t={rb[5]:02x} v={rb[6]:02x}' if rb else '-'
    print(f'{fa:#09x} {fb:#09x} | {fa_str:16s} | {fb_str}')

print('\nRepeat FeatureSet entries 4,25,27,33 x3 for stability:')
FS = 0x01
for i in (4, 25, 27, 33):
    got = []
    for _ in range(3):
        r = c.transact(RID_SHORT, FS, 0x10, bytes([i]))
        got.append(hx(r[4:8]) if r else '-')
    print(f'  i={i:2d}: ' + ' | '.join(got))

print('\nProbe out-of-range entries:')
for i in (36, 40, 0xff):
    r = c.transact(RID_SHORT, FS, 0x10, bytes([i]))
    print(f'  i={i:#04x}: {hx(r) if r else "-"}')

c.close()
