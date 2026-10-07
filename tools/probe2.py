#!/usr/bin/env python3
"""Probe v2: disciplined HID++ transactions against Logitech X2 SUPERSTRIKE.

Improvements over v1:
- drain stale RX backlog before each transaction
- match replies to request signature (devIdx, feature, funcsw)
- retries, deduped logging
- full FeatureSet enumeration + Root.GetFeature lookups for known features
"""
import os
import sys
import time
import select

RID_SHORT, RID_LONG = 0x10, 0x11
LEN = {RID_SHORT: 7, RID_LONG: 20}


def hx(b):
    return ' '.join(f'{x:02x}' for x in b)


class Client:
    def __init__(self, path='/dev/hidraw8'):
        self.path = path
        self.reopen()

    def reopen(self):
        try:
            os.close(self.fd)
        except Exception:
            pass
        self.fd = os.open(self.path, os.O_RDWR | os.O_NONBLOCK)
        self.drain(0.3, verbose=False)

    def close(self):
        os.close(self.fd)

    def drain(self, seconds=0.3, verbose=True):
        end = time.monotonic() + seconds
        drained = 0
        while time.monotonic() < end:
            r, _, _ = select.select([self.fd], [], [], max(0, end - time.monotonic()))
            if not r:
                break
            try:
                n = os.read(self.fd, 64)
            except (BlockingIOError, OSError):
                break
            if not n:
                break
            drained += 1
            if verbose:
                print(f'    rx(passive) {hx(n)}')
        return drained

    def tx_rx(self, pkt, match, timeout=0.4, retries=2, quiet_gap=0.04, verbose=False):
        """Send pkt (bytes w/ report id). Reply must satisfy match(reply_bytes). Returns list of replies."""
        rid = pkt[0]
        assert len(pkt) == LEN.get(rid), f'bad len {len(pkt)} for rid {rid:#x}'
        for attempt in range(retries + 1):
            self.drain(0.02, verbose=False)
            os.set_blocking(self.fd, False)
            try:
                os.write(self.fd, bytes(pkt))
            except OSError as e:
                if verbose:
                    print(f'    write err {e}')
                self.reopen()
                continue
            deadline = time.monotonic() + timeout
            last_err = None
            while time.monotonic() < deadline:
                r, _, _ = select.select([self.fd], [], [], max(0, deadline - time.monotonic()))
                if not r:
                    break
                try:
                    n = os.read(self.fd, 64)
                except (BlockingIOError, OSError):
                    continue
                if not n:
                    continue
                if match(n):
                    if verbose:
                        print(f'    rx(match) {hx(n)}')
                    return [bytes(n)]
                else:
                    if verbose:
                        print(f'    rx(other)  {hx(n)}')
            # timeout — retry
        return []

    # ---- HID++ helpers -------------------------------------------------
    @staticmethod
    def req(rid, dev, feature, funcsw, params=b''):
        ln = LEN[rid]
        total_payload = 3 - 1 + len(params)  # dev,feat,fnsw minus rid... compute explicitly
        pkt = bytearray(LEN[rid])
        pkt[0] = rid
        pkt[1] = dev & 0xff
        pkt[2] = feature & 0xff
        pkt[3] = funcsw & 0xff
        body = bytes(params)[:LEN[rid] - 4]
        pkt[4:4 + len(body)] = body
        return bytes(pkt)

    @staticmethod
    def sig_checker(rid_want, dev_echo_ok, feature_want, funcsw_want):
        def m(rx):
            if len(rx) < 4:
                return False
            if rx[0] not in (RID_SHORT, RID_LONG):
                return False   # skip foreign streams (0x02 beacons, mouse data...)
            # accept echo of whichever dev index; require feature+funcsw match
            if rx[2] == feature_want and rx[3] == funcsw_want:
                return True
            return False
        return m

    def transact(self, rid, feature, funcsw, params=b'', dev=0x01, timeout=0.4, retries=2, verbose=False):
        pkt = self.req(rid, dev, feature, funcsw, params)
        check = self.sig_checker(rid, dev, feature, funcsw)
        out = self.tx_rx(pkt, check, timeout=timeout, retries=retries, verbose=verbose)
        return out[0] if out else None

    # Feature ops
    def root_get_feature(self, fid16, dev=0x01, long=False):
        rid = RID_LONG if long else RID_SHORT
        return self.transact(rid, 0x00, 0x00, bytes([(fid16 >> 8) & 0xff, fid16 & 0xff]), dev=dev)

    def ping(self, dev=0x01, param=0x00):
        return self.transact(RID_SHORT, 0x00, 0x10, bytes([param]), dev=dev)

    def proto_ver(self, dev=0x01):
        return self.transact(RID_LONG, 0x00, 0x30, b'\x02', dev=dev)  # extended form

    def fs_count(self, fs_idx, dev=0x01):
        return self.transact(RID_SHORT, fs_idx, 0x00, b'', dev=dev)

    def fs_by_index(self, fs_idx, i, dev=0x01):
        return self.transact(RID_SHORT, fs_idx, 0x10, bytes([i]), dev=dev)


KNOWN_FEATURES = {
    0x0000: 'Root', 0x0001: 'FeatureSet', 0x0002: 'DeviceInfo(DJ)', 0x0003: 'DeviceInfo',
    0x0004: 'PeripheralDeviceInfo', 0x0005: 'DeviceName', 0x0007: 'ConfigChange?',
    0x0008: 'firmware?', 0x0009: '?unknown-9', 0x000a: 'MousePointer',
    0x0020: '?profile?', 0x0021: 'ConfigurableProps?',
    0x0050: 'Reset?', 0x1000: 'BatteryLevelStatus', 0x1001: 'BatteryVoltage',
    0x1004: 'UnifiedBattery', 0x1006: 'BatteryCapacity?', 0x1010: 'ChargingStatus?',
    0x1300: 'LightingRoot?', 0x1400: 'LowResWheel?', 0x2120: 'TouchpadWallpapers?',
    0x2200: 'AdjustablePointerSpeed', 0x2201: 'AdjustableDPI', 0x2202: 'AdjustablePointerSpeed?',
    0x2203: 'AdjustableReportRate', 0x2204: '?Extreme?',
    0x2250: 'XYStats?', 0x4520: '?',
    0x8050: 'Gesture?', 0x8060: 'ReportRate?', 0x8061: 'ReportRate2?',
    0x8070: 'ColorLEDfx?', 0x8071: 'OnboardMode?', 0x8100: 'MouseButtonSpy',
    0x8110: 'OnboardProfiles?', 0x8040: 'HostConnection?', 0x8001: 'Hub?',
    0x1900: 'LatencyMonitoring', 0x1981: 'Latency?', 0x1b00: 'DFULegacy',
    0x00c2: 'DFU', 0x1f03: 'DFU2', 0x1e00: '?,', 0x1eb9: 'GFTrick?',
}


def main():
    c = Client('/dev/hidraw8')
    try:
        print('== drain passive stream (2s) ==')
        c.drain(2.0, verbose=True)

        print('\n== ping (Root.fn=1, param 0x55) ==')
        for dev in (0x01, 0xff, 0x00):
            r = c.ping(dev=dev, param=0x55)
            print(f'  dev={dev:#04x} ->', hx(r) if r else '(no match)')
            if r:
                break

        print('\n== Root.GetProtocolVersion (fn 3, ext=0x02) ==')
        r = c.proto_ver()
        print('  ->', hx(r) if r else '(no match)')

        print('\n== Root.GetFeature(FEATURE_SET=0x0001) ==')
        r = c.root_get_feature(0x0001)
        print('  ->', hx(r) if r else '(no match)')
        fs_idx = None
        if r and len(r) >= 8:
            # response short: rid dev feat fnsw | params: [featIdx, verHi(ver/type), verLo, ?]
            fs_idx = r[4]
            print(f'  FEATURE_SET at index 0x{fs_idx:02x}, version/type {r[5]:02x}/{r[6]:02x}')

        if fs_idx:
            print('\n== FeatureSet.GetCount ==')
            r = c.fs_count(fs_idx)
            count = r[4] if r else 0
            print(f'  count = {count}')

            print('\n== FeatureSet entries ==')
            entries = {}
            for i in range(count + 1):
                rr = c.fs_by_index(fs_idx, i)
                if not rr:
                    continue
                idx, hi, lo = rr[4], rr[5], rr[6]
                fid = (hi << 8) | lo
                entries[idx] = fid
                name = KNOWN_FEATURES.get(fid, '')
                print(f'  [{i:2d}] idx=0x{idx:02x} feature=0x{fid:04x} ver={rr[7]:02x} {name}')
        else:
            print('!! no FeatureSet — fall back scan: querying Root.GetFeature for known ids')
            for fid in sorted(KNOWN_FEATURES):
                if 0x0000 == fid or 0x0001 == fid:
                    continue
                r = c.root_get_feature(fid)
                if r and r[4] != 0 and not (r[4] == 0 and r[5] == 0):
                    print(f'  0x{fid:04x} {KNOWN_FEATURES[fid]:22s} -> idx=0x{r[4]:02x} ver={r[5]:02x}/{r[6]:02x}')

        print('\n== sanity: DeviceName feature 0x0005 attempt ==')
        r = c.root_get_feature(0x0005)
        print('  ->', hx(r) if r else '(no match)')
        if r and r[4]:
            nm_idx = r[4]
            rr = c.transact(RID_LONG, nm_idx, 0x00, b'')  # GetNameLength? fn0
            print('  len? ->', hx(rr) if rr else '(no match)')
            rr = c.transact(RID_LONG, nm_idx, 0x10, b'\x00')  # getName charIndex 0
            print('  name ->', repr(bytes(rr[4:] if rr else [])) if rr else '(no match)')

        print('\n== passive stream again after traffic (2s) ==')
        c.drain(2.0, verbose=True)
    finally:
        c.close()


if __name__ == '__main__':
    main()
