#!/usr/bin/env python3
"""Guided phased capture: correlate 02-stream & HID++ events with instructed phases.

Phases (total ~55s): IDLE -> MOVE-RIGHT -> MOVE-LEFT -> CLICK-LEFT(force vary)
-> CLICK-RIGHT -> SCROLL. Byte-level stats per phase; full packets to file.
"""
import os, sys, time, select, collections
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

OUT = open(os.path.join(_ROOT, 'research/phases.cap'), 'w')
T0 = time.monotonic()

PHASES = [
    ('IDLE', 'KEEP HANDS OFF — totally still', 8),
    ('RIGHT', 'MOVE MOUSE STEADILY RIGHTWARD (across the desk, ~5s strokes)', 10),
    ('LEFT', 'MOVE STEADILY LEFTWARD (back)', 10),
    ('CLICK_L', 'CLICK LEFT BUTTON rhythmically — soft, medium, hard presses', 12),
    ('CLICK_R', 'CLICK RIGHT BUTTON rhythmically', 8),
    ('SCROLL', 'SCROLL WHEEL up/down', 7),
]

STATS = collections.defaultdict(lambda: collections.Counter())
PHASE_EVENTS = collections.defaultdict(collections.Counter)

def phase_capture(fd, name, instr, secs):
    print(f'\n>>> PHASE {name} ({secs}s): {instr}')
    end = time.monotonic() + secs
    n_pk = 0
    while time.monotonic() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            d = os.read(fd, 64)
        except OSError:
            continue
        if not d:
            continue
        n_pk += 1
        ts = (time.monotonic() - T0) * 1000.0
        b = bytes(d)
        OUT.write(f'{ts:9.1f} {name:8s} {b.hex(" ")}\n')
        if b[0] == 0x02:
            # decompose: hdr, sign fields
            STAT_KEY = tuple(str(x) for x in (
                'hdr=%d' % b[1],
                'A=%d' % int.from_bytes(b[3:5], 'little', signed=True),
                'B=%d' % int.from_bytes(b[5:7], 'little', signed=True),
                'C=%d' % b[7], 'D=%d' % b[8],
                't7=%d' % b[12], 't8=%d' % b[13],
            ))
            for k in STAT_KEY:
                STATS[name][k] += 1
        elif b[0] == 0x11 and len(b) >= 4:
            PHASE_EVENTS[name][f'idx={b[2]:02x} fn={b[3]:02x} p={b[4:12].hex(" ")}'] += 1
        else:
            PHASE_EVENTS[name]['OTHER:' + b.hex(' ')[:40]] += 1
    print(f'    ({n_pk} pkts)')

def fd_for(path='/dev/hidraw8'):
    return os.open(path, os.O_RDONLY | os.O_NONBLOCK)

fd = fd_for()
# flush backlog
while select.select([fd], [], [], 0.1)[0]:
    os.read(fd, 64)

for name, instr, secs in PHASES:
    phase_capture(fd, name, instr, secs)

OUT.close()
print('\n================ PHASE SIGNATURES (nonzero fields only) ================')
common = set.intersection(*[set(v.keys()) for v in STATS.values()]) if STATS else set()
for ph in STATS:
    print(f'-- {ph}:')
    for k, n in sorted(STATS[ph].items()):
        star = '' if k in common else ' *'
        print(f'   {k:14s} x{n}{star}')
print('\n================ EVENT-PAYLOADS PER PHASE (excluding known heartbeat) ================')
for ph in PHASE_EVENTS:
    interesting = {k: v for k, v in PHASE_EVENTS[ph].items()
                   if not k.startswith('idx=00 fn=0a') and not k.startswith('idx=09 fn=5a p=00 03 20 03')}
    if interesting:
        print(f'-- {ph}:')
        for k, v in sorted(interesting.items(), key=lambda kv: -kv[1])[:12]:
            print(f'   x{v:<4d} {k}')
