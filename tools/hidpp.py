#!/usr/bin/env python3
"""Minimal HID++ toolkit over /dev/hidraw for Logitech X2 SUPERSTRIKE (046d:40bd).

Pure stdlib. Speak HID++ 2.0 short (0x10, 7 bytes) and long (0x11, 20 bytes)
reports. Safe: read-only queries only in this module.
"""
import fcntl
import os
import struct
import select
import sys
import time

HIDRAW_MAX_INPUT = 4096

# --- ioctl consts from <linux/hidraw.h> ---
HIDIOWGRDESC = None  # assembled below


def _ior(ty, nr, size):
    # _IOR on Linux/x86: direction _IOC_READ == 2
    return (0x80000000 | (size << 16) | (ord(ty) << 8) | nr)


class hidraw_report_descriptor:
    def __init__(self):
        self.size = 0
        self.value = bytearray(HIDRAW_MAX_INPUT)


def grdesc(fd=None, path='/dev/hidraw8'):
    """Return the raw HID report descriptor bytes (via sysfs)."""
    num = path.rstrip('/').split('/')[-1]          # e.g. hidraw8
    real = os.path.realpath(f'/sys/class/hidraw/{num}')
    base = real.split('/hidraw/')[0]               # .../0003:VID:PID.NNNN
    with open(os.path.join(base, 'report_descriptor'), 'rb') as f:
        return f.read()


SHORT = 0x10  # 7 bytes total: id, dev, feat, func<<4|swid, p0,p1,p2
LONG = 0x11   # 20 bytes


class HidPP:
    def __init__(self, path='/dev/hidraw8'):
        self.fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
        self.path = path
        self.dev_index = None  # discovered

    def close(self):
        os.close(self.fd)

    def send_recv(self, report, wait=0.25):
        """Write raw HID++ report (bytes, WITH report id), collect replies until quiet."""
        os.set_blocking(self.fd, False)
        try:
            os.write(self.fd, bytes(report))
        except OSError as e:
            return [], e
        outs = []
        deadline = time.monotonic() + wait
        buf = bytearray(LONG)
        while True:
            timeout = deadline - time.monotonic()
            if timeout <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], timeout)
            if not r:
                break
            try:
                n = os.read(self.fd, LONG)
            except BlockingIOError:
                continue
            except OSError:
                break
            if n:
                outs.append(bytes(n))
                # keep draining a little longer to catch follow-ups
                deadline = max(deadline, time.monotonic() + 0.05)
        return outs, None

    def request(self, dev, feature, func_swid, params=b'', long=False, wait=0.25):
        rid = LONG if long else SHORT
        body_len = 19 if long else 6
        pad = body_len - 2 - len(params)
        pkt = bytes([rid, dev & 0xff, feature & 0xff, func_swid & 0xff]) + bytes(params[:body_len - 2])
        pkt += b'\x00' * (body_len - 2 - len(pkt) + 3)  # ensure total: 1+2+len(params)+pad
        pkt = (pkt + b'\x00' * (body_len + 1))[:body_len + 1]
        outs, err = self.send_recv(pkt, wait)
        return outs, err

    # convenience
    def ping(self, dev, wait=0.25):
        # Root.Ping: feature 0x00, func 1, param0 = 0x00
        return self.request(dev, 0x00, 0x10, b'\x00', wait)

    def protocol_version(self, dev, wait=0.25):
        # Root.GetProtocolVersion: func 3 (HID++2.0): params 1 = 0x00? (v2: [0])
        return self.request(dev, 0x00, 0x30, b'\x00', wait)

    def get_feature(self, dev, fid16, wait=0.25):
        # Root.GetFeature(func 0): params = 16-bit BE feature id
        return self.request(dev, 0x00, 0x00, bytes([(fid16 >> 8) & 0xff, fid16 & 0xff]), wait)


def hx(b):
    return ' '.join(f'{x:02x}' for x in b)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/dev/hidraw8'
    h = HidPP(path)
    print(f'# opened {path}')
    d = grdesc(path=path)
    print(f'# report descriptor ({len(d)} bytes):')
    for i in range(0, len(d), 16):
        print('# ', hx(d[i:i + 16]))
    print()

    for dev in (0xff, 0x00, 0x01, 0xfe):
        outs, err = h.ping(dev)
        tag = 'OK ' if outs else '-- '
        print(f'[{tag}] ping dev=0x{dev:02x}: ', ' | '.join(hx(o) for o in outs) if outs else (f'err={err}'))
    print()

    # GET PROTOCOL VERSION on promising indexes
    for dev in (0xff, 0x00, 0x01):
        outs, _ = h.protocol_version(dev)
        if outs:
            print(f'proto-version dev=0x{dev:02x}:', ' | '.join(hx(o) for o in outs))
    h.close()


if __name__ == '__main__':
    main()
