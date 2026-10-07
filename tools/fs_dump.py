#!/usr/bin/env python3
"""Strict FeatureSet enumeration: echo-verify query index, dump raw reply params."""
import sys
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import time
sys.path.insert(0, os.path.join(_ROOT, 'tools'))
from probe2 import Client, hx, RID_SHORT, RID_LONG

c = Client('/dev/hidraw8')

# FeatureSet index: confirmed 0x01 (v2). DeviceName at 0x03 (v5).
FS = 0x01

def strict(rid, feature, funcsw, params, echo_slot=None, echo_val=None, timeout=0.5):
    """Send once; return first reply whose params echo echo_val at echo_slot."""
    pkt = Client.req(rid, 0x01, feature, funcsw, params)
    c.drain(0.05, verbose=False)
    try:
        os_write_ok = True
    except Exception:
        os_write_ok = False
    import os as _os
    _os.set_blocking(c.fd, False)
    _os.write(c.fd, pkt)
    import select as _sel
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        r, _, _ = _sel.select([c.fd], [], [], max(0, deadline - time.monotonic()))
        if not r:
            break
        try:
            n = c.fd_read()
        except AttributeError:
            n = _os.read(c.fd, 64)
        except Exception:
            break
        if not n or len(n) < 5:
            continue
        if n[0] not in (RID_SHORT, RID_LONG):
            continue
        if n[2] != feature or n[3] != funcsw:
            continue
        if echo_slot is not None and (len(n) <= echo_slot or n[echo_slot] != echo_val):
            continue
        return bytes(n)
    return None

print('=== FeatureSet.GetFeatureCount (fn0) ===')
r = strict(RID_SHORT, FS, 0x00, b'', echo_slot=2, echo_val=FS)
print(' ', hx(r))
cnt = r[4] if r else 0
print('  count=', cnt)

print('\n=== entries (strict echo on param0) ===')
table = []
for i in range(cnt + 1):
    r = strict(RID_SHORT, FS, 0x10, bytes([i]), echo_slot=4, echo_val=i)
    if r is None:
        # maybe echo in other slot — take any match
        r = strict(RID_SHORT, FS, 0x10, bytes([i]), timeout=0.3)
    print(f'  i={i:2d} -> {hx(r) if r else "(none)"}')
    if r:
        table.append((i, r))

print('\n=== interpret: try fid at (p1,p2) & (p2,p3) & p0,idx variants ===')
print('idx | p0   p1   p2   p3   p4  | A:(idx,p1p2,BE)  B:(p1p2,BE)      C:(p0p1 BE)')
for i, r in table[:40]:
    p = r[4:9]
    a = (r[4], (r[5] << 8) | r[6])
    b = ((r[5] << 8) | r[6], r[7])
    cc = (r[4] << 8) | r[5]
    print(f' {i:2d} | ' + ' '.join(f'{x:02x}' for x in p) + f' | idx={a[0]:02x} fid={a[1]:04x} | fid={b[0]:04x} ??{b[1]:02x} | {cc:04x}')

c.close()
