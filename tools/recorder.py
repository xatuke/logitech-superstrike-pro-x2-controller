#!/usr/bin/env python3
"""Interaction recorder: correlates physical mouse activity with HID++ events.

Captures from:
  - /dev/hidraw8 (device HID++ node; feature events, analog key data)
  - /dev/hidraw7 (receiver if2 = raw mouse reports of paired device: motion+buttons ground truth)
for N seconds, timestamps every packet, and prints a transition summary.

Non-invasive: reads only.
"""
import os
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import sys
import time
import select

T0 = time.monotonic()

IDX_NAMES = {
    0x01: 'FeatureSet', 0x02: 'DevInfo', 0x03: 'Name', 0x04: '?A(1d4b)',
    0x05: '?0x20', 0x06: 'Battery', 0x07: '?2250', 0x08: '?2251', 0x09: '?2202-sensor',
    0x0a: '?8090', 0x0b: '?80e0', 0x0c: '?1b0c', 0x0d: '?8061-rate',
    0x0e: '?8100-keys', 0x0f: '?8110-onboard', 0x10: '?1500-light',
    0x11: '?1801', 0x12: '?1802', 0x13: '?1803', 0x14: '?1806', 0x15: '?1817',
    0x16: '?1805', 0x17: '?1830', 0x18: '?1877', 0x19: '?9403', 0x1a: '?1861',
    0x1b: '?1890', 0x1c: '?18a1', 0x1d: '?1e00-adc', 0x1e: '?1e02', 0x1f: '?1e22',
    0x20: '?1e30', 0x21: '?1602', 0x22: '?1eb0', 0x23: '?18b1',
}

SUMMARY = {}


def note(tag, data):
    SUMMARY.setdefault(tag, []).append(data)


def classify(raw, src):
    ts = (time.monotonic() - T0) * 1000.0
    b = bytes(raw)
    if b and b[0] == 0x02:
        note('beacon02', (ts, b.hex(' ')))
        return
    if b and b[0] == 0x11 and len(b) >= 6:
        idx = b[2]
        name = IDX_NAMES.get(idx, f'?{idx:02x}')
        note(f'evt:{name}', (ts, b.hex(' ')))
        print(f'[{ts:8.1f}ms] EV  {name:12s} {b.hex(" ")[:90]}')
        return
    if b and b[0] == 0x10 and len(b) >= 4:
        note('short10', (ts, b.hex(' ')))
        print(f'[{ts:8.1f}ms] S10 {" ":12s} {b.hex(" ")[:90]}')
        return
    note(f'raw{src}', (ts, b.hex(' ')))


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
    out = open(os.path.join(_ROOT, 'research/session.cap'), 'w')
    fds = {}
    for path, tag in (('/dev/hidraw7', 'MOUSE'), ('/dev/hidraw8', 'HIDPP')):
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
            fds[fd] = tag
        except OSError as e:
            print(f'(cannot open {path}: {e})')
    print(f'RECORDING {secs:.0f}s — wiggle the mouse, click buttons (hold/press patterns), now!')
    end = time.monotonic() + secs
    nxt_print = time.monotonic() + 5
    # silence: only print state-CHANGING packets
    last_mouse_state = None
    last_feature_payload = {}

    while time.monotonic() < end:
        r, _, _ = select.select(list(fds), [], [], 0.25)
        for fd in r:
            tag = fds[fd]
            try:
                data = os.read(fd, 64)
            except (BlockingIOError, OSError):
                continue
            if not data:
                continue
            ts = (time.monotonic() - T0) * 1000.0
            if tag == 'MOUSE':
                # raw mouse report: detect button byte transitions; suppress pure-motion spam
                st = bytes(data[:1] + data[-1:])
                key = bytes(data)
                if data and data[0] == 0x02:
                    btn = data[1] & 0x07
                    if btn != last_mouse_state:
                        print(f'[{ts:8.1f}ms] BTN {bin(btn)}')
                        note('BTN', (ts, bin(btn)))
                        last_mouse_state = btn
                out.write(f'{ts:9.1f} M {data.hex(" ")}\n')
            else:
                out.write(f'{ts:9.1f} H {data.hex(" ")}\n')
                classify(data, tag)
        if time.monotonic() > nxt_print:
            print(f'  ... {(time.monotonic()-T0):.0f}s elapsed')
            nxt_print += 5
    out.close()

    print('\n==== TRANSITION SUMMARY ====')
    for tag in sorted(SUMMARY):
        vals = SUMMARY[tag]
        uniq = {}
        for ts, hx in vals:
            uniq.setdefault(hx, [0, ts, ts])
            uniq[hx][0] += 1
            uniq[hx][2] = ts
        print(f'{tag}: {len(vals)} pkts, {len(uniq)} unique')
        for hx, (n, t0, t1) in sorted(uniq.items()):
            print(f'    x{n:<4d} t={t0:7.1f}->{t1:7.1f}ms  {hx}')


if __name__ == '__main__':
    main()
