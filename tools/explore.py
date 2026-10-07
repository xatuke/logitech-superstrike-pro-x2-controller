#!/usr/bin/env python3
"""Explore remaining unknowns read-only: dpi list pages, bhop, mode status, button-2 hits."""
import sys
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, 'tools'))
from probe2 import Client, hx, RID_SHORT, RID_LONG

c = Client('/dev/hidraw8')

def call(fidx, fn, params=b'', rid=RID_SHORT):
    return c.transact(rid, fidx, fn << 4, params)

print('== DPI (idx 09) fn2 GET_SENSOR_DPI_LIST pages, sensor=0 axis=0 ==')
for page in (0, 1, 2):
    r = call(0x09, 0x02, bytes([0, 0, page]))
    print(f'  page {page}: {hx(r) if r else "-"}')
print('== DPI axis=1 ==')
for page in (0, 1):
    r = call(0x09, 0x02, bytes([0, 1, page]))
    print(f'  page {page}: {hx(r) if r else "-"}')
print('== DPI fn5 GET_SENSOR_DPI sensor0 (long) ==')
r = call(0x09, 0x05, bytes([0]), RID_LONG)
print('  ', hx(r) if r else '-')
print('== DPI fn1 AXES sensor0 ==')
r = call(0x09, 0x01, bytes([0]))
print('  ', hx(r) if r else '-')
print('== DPI fn0 COUNT ==')
r = call(0x09, 0x00)
print('  ', hx(r) if r else '-')

print('== BHOP 0x80e0 (idx 0b) fn0/1/2 with 0..2 ==')
for fn in range(3):
    r = call(0x0b, fn)
    print(f'  fn{fn} (): {hx(r) if r else "-"}')
    for p in (0, 1, 2):
        r = call(0x0b, fn, bytes([p]))
        print(f'  fn{fn} ({p}): {hx(r) if r else "-"}')

print('== MODE STATUS 0x8090 (idx 0a) fn0..3 ==')
for fn in range(4):
    r = call(0x0a, fn)
    print(f'  fn{fn} (): {hx(r) if r else "-"}')
    for p in (0, 1, 2):
        r = call(0x0a, fn, bytes([p]))
        print(f'  fn{fn} ({p}): {hx(r) if r else "-"}')

print('== HITS button 2? ==')
r = call(0x0c, 0x02, bytes([2]))
print('  fn2(btn2):', hx(r) if r else '-')

print('== SPY 0x8110 (idx 0f) fn0..5, empty + params ==')
for fn in range(6):
    r = call(0x0f, fn)
    print(f'  fn{fn} (): {hx(r) if r else "-"}')
    for p in (0, 1):
        r = call(0x0f, fn, bytes([p]))
        print(f'  fn{fn} ({p}): {hx(r) if r else "-"}')

print('== ONBOARD 0x8100 (idx 0e) fn0..7 empty ==')
for fn in range(8):
    r = call(0x0e, fn)
    print(f'  fn{fn} (): {hx(r) if r else "-"}')

print('== XY stats 0x2250 (idx 07) / wheel 0x2251 (idx 08) fn0..2 ==')
for idx, nm in ((0x07, 'XY'), (0x08, 'WHEEL')):
    for fn in range(3):
        r = call(idx, fn)
        print(f'  {nm} fn{fn}: {hx(r) if r else "-"}')

print('== ADC 0x1e00 (idx 1d) fn0/1 ==')
for fn in range(2):
    r = call(0x1d, fn, rid=RID_LONG)
    print(f'  fn{fn}: {hx(r) if r else "-"}')

c.close()
