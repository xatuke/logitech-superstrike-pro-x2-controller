#!/usr/bin/env python3
"""Sweep: for each known feature index, probe funcs 0..5 (short/long GET-like) and
record replies. DeviceInfo + UnifiedBattery + rate-ish features get structured reads.
ALL operations are read/query only.
"""
import sys, time
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, 'tools'))
from probe2 import Client, hx, RID_SHORT, RID_LONG

FEATS = {
    0x01: (0x0001, 'FeatureSet'),
    0x02: (0x0003, 'DeviceInfo v7'),
    0x03: (0x0005, 'DeviceName v5'),
    0x04: (0x1d4b, '?unknown-A'),
    0x05: (0x0020, '?0x0020'),
    0x06: (0x1004, 'UnifiedBattery v5'),
    0x07: (0x2250, '?dpi-cluster'),
    0x08: (0x2251, '?dpi-cluster2'),
    0x09: (0x2202, 'AdjPtrSpeed'),
    0x0a: (0x8090, '?8090-v3'),
    0x0b: (0x80e0, '?80e0'),
    0x0c: (0x1b0c, '?1b0c'),
    0x0d: (0x8061, '?rate-ish'),
    0x0e: (0x8100, '?btnspy-ish'),
    0x0f: (0x8110, '?onboard-ish'),
    0x10: (0x1500, '?lighting-ish'),
    0x11: (0x1801, '?18blk-01'),
    0x12: (0x1802, '?18blk-02'),
    0x13: (0x1803, '?18blk-03-v1'),
    0x14: (0x1806, '?18blk-06-v8'),
    0x15: (0x1817, '?18blk-17'),
    0x16: (0x1805, '?18blk-05'),
    0x17: (0x1830, '?18blk-30'),
    0x18: (0x1877, '?18blk-77'),
    0x19: (0x9403, '?9403'),
    0x1a: (0x1861, '?18blk-61-v1'),
    0x1b: (0x1890, '?18blk-90'),
    0x1c: (0x18a1, '?18blk-a1'),
    0x1d: (0x1e00, 'ADC-measure'),
    0x1e: (0x1e02, '?1e02'),
    0x1f: (0x1e22, '?1e22-v1'),
    0x20: (0x1e30, '?1e30'),
    0x21: (0x1602, '?1602'),
    0x22: (0x1eb0, '?1eb0'),
    0x23: (0x18b1, '?18blk-b1'),
}

ERR_NO = {0x02: 'INVALID_FEATURE', 0x03: 'INVALID_VALUE', 0x04: 'HIGH_RESOURCES?',
          0x05: 'LOW_RESOURCES', 0x08: 'BUSY', 0x09: 'COMM?', 0x0a: 'UNSUPPORTED_PARAM'}

def decode_params(p):
    if p and p[0] in ERR_NO:
        return f'<ERR {p[0]:02x} {ERR_NO[p[0]]}> {hx(p[1:])}'.rstrip()
    return hx(p).rstrip() if any(p) else '.'

c = Client('/dev/hidraw8')
OUT = []
def log(*a):
    s = ' '.join(str(x) for x in a)
    OUT.append(s)
    print(s)

log('=== per-feature function probe (funcs 0..5, short then long, empty params) ===')
for idx in sorted(FEATS):
    fid, name = FEATS[idx]
    log(f'\n-- feat idx=0x{idx:02x} fid=0x{fid:04x} {name}')
    for fn in range(6):
        fsw_s = (fn << 4)
        rs = c.transact(RID_SHORT, idx, fsw_s, b'', timeout=0.25, retries=0)
        rl = c.transact(RID_LONG, idx, fsw_s, b'', timeout=0.25, retries=0)
        line = f'   fn{fn} short: {decode_params(rs[4:]) if rs else "-"}'
        if rl:
            line += f'   long: {decode_params(rl[4:])}'
        log(line)

log('\n=== DeviceInfo (idx 0x02) structured ===')
# v7: func0 = getInfo? Standard v3+: fn0 getInfo -> type,serial...; fn1 GetSerial; fn2 GetModelId...
for fn in range(4):
    r = c.transact(RID_LONG, 0x02, fn << 4, b'', timeout=0.3, retries=1)
    log(f'  fn{fn}: {hx(r) if r else "-"}')
# fn1 serialNumberDigits? fn with params for unit?
r = c.transact(RID_LONG, 0x02, 0x10, bytes([0x01]), timeout=0.3)
log(f'  fn1(=GetSerial?): {hx(r) if r else "-"}')
r = c.transact(RID_LONG, 0x02, 0x20, bytes([0x01]), timeout=0.3)  # model id
log(f'  fn2(modelId): {hx(r) if r else "-"}')

log('\n=== UnifiedBattery (idx 0x06) structured ===')
r = c.transact(RID_SHORT, 0x06, 0x00, b'', timeout=0.3)
log(f'  fn0 getStatus: {hx(r) if r else "-"}')
r = c.transact(RID_SHORT, 0x06, 0x10, b'', timeout=0.3)
log(f'  fn1 getCapability?: {hx(r) if r else "-"}')

log('\n=== DeviceName (idx 0x03): length + chunks ===')
r = c.transact(RID_SHORT, 0x03, 0x00, b'', timeout=0.3)
log(f'  fn0 getNameLength: {hx(r) if r else "-"}')
nm = b''
for ci in range(4):
    r = c.transact(RID_LONG, 0x03, 0x10, bytes([ci * 16]), timeout=0.3)
    if r:
        nm += bytes(r[4:20])
log(f'  NAME = {nm!r}')

log('\n=== 0x8061 (idx 0x0d) rate probe: try params 00..05 fn0 ===')
for p in range(6):
    r = c.transact(RID_SHORT, 0x0d, 0x00, bytes([p]), timeout=0.25, retries=0)
    log(f'  fn0(p={p}): {decode_params(r[4:]) if r else "-"}')

log('\n=== 0x2250/0x2251/0x2202 DPI cluster fn0..2 ===')
for idx in (0x07, 0x08, 0x09):
    fid, name = FEATS[idx]
    for fn in range(3):
        for prm in (b'', b'\x00', b'\x01'):
            if not prm:
                continue  # empty already swept
            r = c.transact(RID_SHORT, idx, fn << 4, prm, timeout=0.25, retries=0)
            if r:
                log(f'  {name} fn{fn} p={prm.hex()} -> {decode_params(r[4:])}')

log('\n=== Passive: gently MOVE NOTES — battery/status events seen while idle ===')
c.drain(1.0, verbose=False)
c.close()

open(os.path.join(_ROOT, 'research/live-sweep.txt'), 'w').write('\n'.join(OUT))
print('\n[saved research/live-sweep.txt]')
