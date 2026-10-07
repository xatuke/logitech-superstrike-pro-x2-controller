# HID++ Protocol Reference — compiled for the Logitech PRO X 2 Superstrike (X2 SUPERSTRIKE)

Compiled 2026-10-07 from primary open-source implementations. Target: wireless gaming mouse
USB VID 0x046d / device WPID `0x40BD`, paired through Logitech LIGHTSPEED receiver USB PID `0xC54D`.
Device self-identifies as **HID++ protocol 4.2** (ping major.minor 4.2), kind `mouse`.

> **Critical correction to the research brief:** `cvut/librehiddocs` does not exist on GitHub
> (clone fails "Repository not found"; GitHub API repo search for `librehiddocs` returns 0 repos;
> the `cvut` org holds no HID repo). The LibreHidDocs name appears to be lost/moved upstream.
> Its successors used here — and credited by Solaar itself as the source of feature names — are:
> `https://github.com/cvuchener/hidpp` and `https://github.com/Logitech/cpg-docs/tree/master/hidpp20`
> (see provenance comment at `Solaar/lib/logitech_receiver/hidpp20_constants.py:22-24`).
> Additionally, **stock Solaar (Aug 2026) already supports this exact mouse**, including its
> HITS hall-effect tuning — device dumps and a CLI reference ship in-tree. Most of the
> protocol below is therefore *device-confirmed*, not inferred.

---

## 1. Reference corpus (clones used, HEADs recorded)

All clones read-only under `downloads/refs/`:

| Repo | URL | HEAD (shallow) | Files mined |
|---|---|---|---|
| Solaar | github.com/pwr-Solaar/Solaar | `e7304c4c451cc9bb4f206a914844525e67856a28` (2026-08-18) | `lib/logitech_receiver/{hidpp20.py,hidpp20_constants.py,settings_templates.py,base.py,base_usb.py,notifications.py}`, `docs/{features.md,capabilities.md,devices.md,PRO_X2_SUPERSTRIKE_CLI.md,devices/PRO X 2 Superstrike 40BD.txt}` |
| Solaar wiki | github.com/pwr-Solaar/Solaar.wiki.git | `9b908ca899dbca17afc11c1523aa7f6bee329db5` | sparse; superseded by `Solaar/docs/` |
| libratbag | github.com/libratbag/libratbag | `8235b5bb6032dea15901ab58a8218956f4494d08` (2026-09-17) | `src/hidpp20.{c,h}`, `src/hidpp10.{c,h}`, `src/driver-hidpp20.c` |
| logiops | github.com/PixlOne/logiops | `e15799553f97c1b8bab5d9b22b58453513b56217` | `src/logid/backend/hidpp20/feature_defs.h`, `features/*` |
| Linux kernel | raw torvalds/linux master | master file fetch | `drivers/hid/hid-logitech-hidpp.c` (5023 lines) → `downloads/refs/kernel/` |

Searched, not found, for the record: **no OSS repo implements "LIGHTFORCE" by that name**
(grep across all five trees = 0 hits). Analog/optical actuation on mice IS exposed, but under
**`ANALOG_BUTTONS 0x1B0C`** — see §5.13. logiops' fork-era enum predates 0x2202/0x8061/0x8100
(`feature_defs.h` tops out around 0x2xxx/0x1d4b and lacks gaming-tier entries).

---

## 2. Transport & message framing (layer 0)

Source: `Solaar/lib/logitech_receiver/base.py:90-121`

| Report ID | Size | Purpose |
|---|---|---|
| `0x10` | 7 B | HID++ short frame |
| `0x11` | 20 B | HID++ long frame |
| `0x20` | DJ/frame | Receiver DJ traffic |
| `0x50`/`0x51` | 64 B | Centurion (audio/headset; **not** used by this mouse) |

Frame layout, short: `[0x10][deviceIndex][featureIdx][funcByte][p0][p1][p2]`;
long adds 15 payload bytes. Responses echo `deviceIndex`; async notifications arrive as
HID++ short/long frames from the device index.

- **Feature request addressing** — request byte pair is `(featureIndex, (function&0x0F)<<4 | softwareId)`.
  Solaar expresses functions pre-shifted as integers (`0x00`= getInfo, `0x10`=read-1, `0x20`=read-2,
  `0x30`=write-1 …). Helper: `hidpp20.py:1754 feature_request()` builds
  `(feature_index << 8) + (function & 0xFF)`.
- **Software ID** — 3 bits in the low nibble of `funcByte`; host correlates requests by randomizing it
  (`base.py:815-824`, ping uses `request_id = 0x0010 | sw_id` plus a random mark byte echoed in the reply).
- **Ping / probe version** — feature `0x0000`, reply params `[major][minor]`; Solaar composes `major + minor/10`
  (`base.py:818-826`). Kernel equivalent `CMD_ROOT_GET_PROTOCOL_VERSION 0x10` OR'd with the kernel
  software id (`kernel/hid-logitech-hidpp.c:947-981`). A HID++ 1.0-only device answers this with a
  short-frame error `0x8f` `INVALID_SUB_ID_COMMAND` — that error is how you distinguish 1.x devices.
- **Errors** — long error frame `0x8f …` carrying `ErrorCode`:
  `1 UNKNOWN, 2 INVALID_ARGUMENT, 3 OUT_OF_RANGE, 4 HARDWARE_ERROR, 5 LOGITECH_ERROR,
  6 INVALID_FEATURE_INDEX, 7 INVALID_FUNCTION, 8 BUSY, 9 UNSUPPORTED`
  (`hidpp20_constants.py:281-291`; raised at `base.py:762` as `FeatureCallError`).
- **Timeouts** — receiver 0.9 s, device 4 s, ping timeout 4 s (`base.py:131-134`).
- On the **c54d Lightspeed receiver**, devices occupy indexes 1..n behind the receiver hidraw node;
  speak the frames above to the *receiver* hidraw with the device index of the paired mouse.
  Receiver classification: `base_usb.py:178` `LIGHTSPEED_RECEIVER_C54D = _lightspeed_receiver(0xC54D)`,
  registry `base_usb.py:205-207`. Same class as c539/c53a/c53d/c53f/c541/c545/c547.
- Raw path from a custom driver: open the receiver (and/or wired USB mode, USB PID `0xC0A8`
  per Model ID `40BDC0A80000`, transport map at `hidpp20.py:1830`) hidraw, use REPORT ids 0x10/0x11;
  no kernel pid quirk is needed to do raw HID++ (the kernel driver claims nothing special for 40BD).

---

## 3. Feature discovery (layer 1)

Universal, all present on 40BD (indexes from live dump, §4):

- **ROOT `0x0000`** — `fn 0x00`: params = feature id 16-bit big-endian → reply byte0 = assigned
  feature *index* on this device (0 if unsupported). Version byte2 of reply.
- **FEATURE_SET `0x0001`** — `fn 0x00` → count of features; `fn 0x10(i)` → `{index, feat_hi, feat_lo, kind}`
  for i-th feature (`Solaar docs/features.md:14-16`; implementation `FeaturesArray.enumerate()` `hidpp20.py:331`).
- **Feature flags** — `INTERNAL = 0x20`, `HIDDEN = 0x40`, `OBSOLETE = 0x80`
  (`hidpp20_constants.py:243-248`, reader `FeaturesArray.get_feature` `hidpp20.py:301-343`).
  The Superstrike exposes hidden/internal features (e.g. `0x1E00` `ENABLE_HIDDEN_FEATURES` is itself
  hidden; unlocking it is a live-RE experiment worth trying — the device also hides
  `9403` [candidate BHOP] and `80E0`).
- **DEVICE_FW_VERSION `0x0003` (V7)** — `fn 0x00` → count; `fn 0x10(idx)`:
  `[level&0x0F][name:3]["!3sBBH" → name, verMajor, verMinor, build u16]…[extras 9:]`;
  level 0/1 = bootloader/firmware "BL2"/"MPM", 2 = hardware. Same call's base reply doubles as ids:
  `unitId = [1:5]`, transport nibble `[6]` (`btid 0x1 / btleid 0x2 / wpid 0x4 / usbid 0x8`),
  `modelId = [7:13]` sliced per transport bit (`hidpp20.py:1821-1834`, `get_firmware` `1771-1798`).
  Live: bootloader `BL2 73.00.B0011`, firmware `MPM 42.00.B0011`, model `40BDC0A80000`.
- **DEVICE_NAME `0x0005` (V5)** — `fn 0x00` → name length; `fn 0x10(offset)` chunks ASCII/UTF-8;
  `fn 0x20` → kind byte (`DEVICE_KIND` map, **mouse = 0x03**, `hidpp20_constants.py:251-260`).
  Reader `hidpp20.py:1836-1870`. Live name string: `PRO X2 SUPERSTRIKE`.
- **DEVICE_FRIENDLY_NAME `0x0007`** — same shape; note chunk slice starts at `fragment[1:]`
  (`hidpp20.py:1875-1894`).
- **CONFIG_CHANGE `0x0020`** (aka Reset/ConfigChange; **V0 present on device**) —
  `fn 0x00` GetCookie → 2-byte cookie; `fn 0x10(lo,hi)` SetComplete — ack host-side config sync.
  Cookie `0x0000` releases the software effect-engine claim on some devices — Solaar deliberately
  never sends it, using a nonzero per-session counter instead (`hidpp20.py:2156-2179`,
  `_session_cookie` at `:1769`). Send SetComplete after pushing profiles/onboard mode changes.
- **DEVICE_RESET `0x1802`** present (factory reset; layout unspecified in OSS — treat cautiously).
- **CHANGE_HOST `0x1814`** — `fn 0x00` → `{nHosts, currentHost,…}`; `fn 0x10(host, no_reply)`
  switches (`settings_templates.py:1295-1307`). **HOSTS_INFO `0x1815`** — caps `[0]&1=get-names,
  [0]&2=set-names`, numHosts `[2]`, currentHost `[3]`; per host `fn 0x10(i)` → `{status,nameLen,maxNameLen}`,
  name in 14-B chunks `fn 0x30(i,off)` / write `fn 0x40(i,off,chunk)` (`hidpp20.py:2064-2112`).
  Neither appears in the 40BD live dump (Lightspeed receivers handle host selection), but include
  defensively — wired/BT paths of the same firmware may expose them.

---

## 4. Ground truth: the 40BD device's actual feature table

Verbatim from **live-device dump shipped in Solaar tree**
(`Solaar/docs/devices/PRO X 2 Superstrike 40BD.txt`, `solaar show` 1.1.19). This is the implementer's
checklist — 36 slots, indexes are the wire feature-index values:

| Idx | Feature | ID | Ver | Notes |
|----|--------|-----|-----|-------|
| 0 | ROOT | 0x0000 | 0 | |
| 1 | FEATURE_SET | 0x0001 | 0 | |
| 2 | DEVICE FW VERSION | 0x0003 | **7** | |
| 3 | DEVICE NAME | 0x0005 | **5** | |
| 4 | WIRELESS DEVICE STATUS | 0x1D4B | 0 | |
| 5 | CONFIG CHANGE | 0x0020 | 0 | |
| 6 | UNIFIED BATTERY | 0x1004 | **5** | sole battery feature on device |
| 7 | XY STATS | 0x2250 | 1 | read-only stats |
| 8 | WHEEL STATS | 0x2251 | 0 | |
| 9 | EXTENDED ADJUSTABLE DPI | 0x2202 | 0 | X:800 Y:800 LOD:HIGH stock |
| 10 | MODE STATUS | 0x8090 | **3** | unspecified in OSS — probe |
| 11 | unknown:80E0 | 0x80E0 | 0 | = `BUNNY_HOPPING` const; dead haptics stub on this unit |
| 12 | **SUPERSTRIKE TUNING** | **0x1B0C** | 0 | HITS analog buttons — §5.13 |
| 13 | **EXTENDED ADJUSTABLE REPORT RATE** | **0x8061** | 0 | 125 µs…8 ms — §5.9 |
| 14 | ONBOARD PROFILES | 0x8100 | 0 | §5.11 |
| 15 | MOUSE BUTTON SPY | 0x8110 | 0 | §5.12 |
| 16 | FORCE PAIRING | 0x1500 | 0 | |
| 17 | unknown:1801 | 0x1801 | 0 | internal, hidden |
| 18 | DEVICE RESET | 0x1802 | 0 | |
| 19 | unknown:1803 | 0x1803 | 0 | internal, hidden |
| 20 | CONFIG DEVICE PROPS | 0x1806 | **8** | |
| 21 | unknown:1817 | 0x1817 | 0 | internal, hidden |
| 22 | OOBSTATE | 0x1805 | 0 | |
| 23 | unknown:1830 | 0x1830 | 0 | internal, hidden |
| 24 | unknown:1877 | 0x1877 | 0 | internal, hidden |
| 25 | unknown:9403 | 0x9403 | 0 | internal, hidden — candidate "BHOP" |
| 26 | unknown:1861 | 0x1861 | 0 | internal, hidden |
| 27 | unknown:1890 | 0x1890 | 0 | internal, hidden |
| 28 | unknown:18A1 | 0x18A1 | 0 | internal, hidden |
| 29 | unknown:1E00 | 0x1E00 | 0 | **ENABLE_HIDDEN_FEATURES**, itself hidden |
| 30 | unknown:1E02 | 0x1E02 | 0 | internal, hidden |
| 31 | unknown:1E22 | 0x1E22 | 0 | internal, hidden |
| 32 | unknown:1E30 | 0x1E30 | 0 | internal, hidden |
| 33 | unknown:1602 | 0x1602 | 0 | |
| 34 | unknown:1EB0 | 0x1EB0 | 0 | internal, hidden |
| 35 | unknown:18B1 | 0x18B1 | 0 | internal, hidden |

Corrections to commonly assumed values, established by this dump:

- **No `ADJUSTABLE_DPI 0x2201`** — only extended `0x2202` (interpret as "2201 replaced by 2202" era).
- **No `REPORT_RATE 0x8060`** — only extended `0x8061`.
- **No `BATTERY_STATUS 0x1000`, no `BATTERY_VOLTAGE 0x1001`** — only `UNIFIED_BATTERY 0x1004`.
- **No REPROG_CONTROLS at all** (0x1B00–0x1B04 absent) — button programming is onboard-profile +
  MOUSE_BUTTON_SPY territory; the analog primaries live in 0x1B0C.
- **No 0x8070/0x8071 lighting features** — RGB is profile-defined (effect records in profile flash,
  §5.11) or device-managed.
- MOUSE_BUTTON_SPY **is 0x8110** (brief-guess "0x8100" collides with ONBOARD_PROFILES).
- ADC_MEASUREMENT is **0x1F20**, not 0x1F00 (constant `hidpp20_constants.py:90`; kernel `:1856`);
  not in this device's enumeration.

Enum source of truth for all names incl. not-used-here values:
`Solaar/lib/logitech_receiver/hidpp20_constants.py:32-240`
(superset corroborated by `libratbag/src/hidpp20.h:97-581` and
`logiops/src/logid/backend/hidpp20/feature_defs.h:34-…`; note logiops mistypes DFU=0xd000).

Master reference prose: `Solaar/docs/features.md` (status per feature; SUPERSTRIKE row at `:53`).

---

## 5. Packet layouts per feature

Encoding conventions: all multi-byte ints big-endian on the wire (HID++ classic) **except**
onboard-profile flash records, which are little-endian (§5.11). Solaar struct fmts quoted verbatim.

### 5.1 Battery family

- **BATTERY_STATUS `0x1000`** `fn 0x00` → `!BBB` = discharge% (0→unknown), next-level%, status byte
  (`BatteryStatus` enum). Decoder `hidpp20.py:2191-2204`.
- **BATTERY_VOLTAGE `0x1001`** `fn 0x00` → `">HB"` voltage-mV + flags byte:
  bit7 recharging (low 2 bits = `ChargeStatus`), bit3 fast-charge, bit4 slow-charge, bit5 critical.
  `hidpp20.py:2207-2241`.
- **UNIFIED_BATTERY `0x1004` (V5, the one this mouse has)**
  - `fn 0x00` capabilities → `params[0]` = bitmask of supported discrete levels, `params[1]`:
    bit0 rechargeable, bit1 **state-of-charge (percentage) supported**
    (kernel `hid-logitech-hidpp.c:1566-1620`).
  - `fn 0x10` status → `!BBBB` = SoC-percent (1-100), discrete-level (bit0 crit, bit1 low,
    bit2 good, bit3 full), status byte (shared `BatteryStatus`), ignore. Solaar decoder
    `hidpp20.py:2244-2265`; level sems: 8 FULL / 4 GOOD / 2 LOW / 1 CRIT.
  - Notification `EVENT … STATUS 0x00` asynchronous (`kernel:1571`).
- **ADC_MEASUREMENT `0x1F20`** `fn 0x00` → `!HB` mV + flags (bit0 valid/charging-info present,
  bit1 recharging). `hidpp20.py:2271-2277`. On some headsets this errors when asleep — catch it.
- **Voltage→percent interpolation table** (Li-ion curve, endpoints 4186 mV=100% … 3500 mV=0%,
  linear between knots) — reusable for any reading in mV: `hidpp20.py:2290-2318`.

### 5.2 MOUSE_POINTER `0x2200` (info)

`fn 0x00` → `!HB` = dpi u16 + flags: bits0-1 acceleration (none/low/med/high), bit2 suggest-OS-ballistics,
bit3 suggest-vertical-orientation. `hidpp20.py:1979-1991`.

### 5.3 ADJUSTABLE_DPI `0x2201` (generic, not on 40BD — include for other HW)

- `fn 0x10(sensorSel, chunkIdx)` (Solaar passes direction in first param for 2202 variant) → DPI **table
  stream**; ignore first byte, concat chunks until `00 00`.
- **Stream grammar** (`settings_templates.py:1035-1058` `produce_dpi_list`): u16BE values;
  `val < 0xE000` → literal DPI; `val >> 13 == 0b111` (0xE000-0xFFFF) → step entry: low 13 bits =
  step size, next u16BE = final value — synthesize `range(prev+step, last+1, step)`.
- `fn 0x20` read → `[cur u16BE][default u16BE]` (offset 1..2 / 3..5 in reply incl. header).
  `fn 0x30` write: prefix `0x00` + dpi u16BE (`settings_templates.py:1061-1086`).

### 5.4 EXTENDED_ADJUSTABLE_DPI `0x2202` (**device-backed**, sensor to 32 000 DPI)

Layout fully pinned by `ExtendedAdjustableDpi` (`settings_templates.py:1088-1168`):

- **Capabilities** `fn 0x10(0x00)` → `reply[1]` = aux, `reply[2]` bits: **bit0 = independent Y axis**,
  **bit1 = LOD supported**.
- **DPI tables** per axis: `fn 0x20(0x00, axis 0|1, chunkIdx)` → same compressed-stream grammar as
  §5.3 with 3 header bytes skipped (`produce_dpi_list(feature, 0x20, 3, device, axis)`, `:1102-1106`).
- **Read all** `fn 0x50` →
  `[hdr][X-cur u16BE @1:3][X-default @3:5][Y-cur @5:7][Y-default @7:9][LOD u8 @9]`
  (zero current field ⇒ use default field). Decode `:1124-1136`.
- **Write** `fn 0x60`: `00` + X u16BE + Y u16BE (zeros if !y) + LOD u8.
  **LOD enum: 0=LOW, 1=MEDIUM, 2=HIGH** (`:1111`, doc `docs/capabilities.md:213`).
- Effective range on this mouse 100-32000 DPI (dump line 9 shows stock 800/800 HIGH; doc `:213`).
- Host-mode caveat identical to classic DPI: requires onboard profiles *disabled* to stick reliably.

### 5.5 HIRES wheel cluster (informational — **absent on 40BD**)

`HI_RES_SCROLLING 0x2120` `fn 0x00` → `!BB` mode,resolution (`hidpp20.py:2009-2013`);
`HIRES_WHEEL 0x2121`: `fn 0x00` caps `{multi u8, flags: bit3 invert/bit4? ratchet(bit2)}`,
`fn 0x10` mode byte (bit0 high-res-target, bit1 smooth-res, bit2 invert), `fn 0x30` ratchet bit0
(`:2030-2054`). Kernel equivalents `:2008-2098`. MOUSE_BUTTON_SPY-era devices often pair these
with `VERTICAL_SCROLLING 0x2100` `!BBB` roller,ratchet,lines (`:1993-2007`).

### 5.6 POINTER_SPEED `0x2205`

`fn 0x00` → u16BE multiplier, **256 = 1.0** (range 0x2E-0x1FF usable); decode `hi + lo/256`
(`hidpp20.py:2015-2021`; setting `PointerSpeed` `settings_templates.py:504-513`).

### 5.7 WIRELESS_DEVICE_STATUS `0x1D4B` (**present**)

Pure-notification feature (kernel registers it only to find its index, `kernel:1841-1855`).
Link/battery/power transitions arrive as short notifications; Solaar consumes general wireless
notification flags here and on hidpp10 channels: `link_encrypted` bit checks `flags&0x80`
(classic) / `0x20` (Bolt) in `notifications.py:194-216`. Expect `XY_STATS 0x2250` /
`WHEEL_STATS 0x2251` telemetry notifications when enabled.

### 5.8 MODE_STATUS `0x8090` (**present, V3, unreversed**)

Constant only (`hidpp20_constants.py:153`). No OSS layout. Probe candidates: report current
operating mode (onboard vs host, DFU, sleep). High-value live-RE target; pairs with the
`80E0` stub.

### 5.9 Report rate: classic vs extended

- **REPORT_RATE `0x8060`** (legacy): `fn 0x00` caps → bitmask byte, bit i (0..7) = allows
  (i+1) ms. `fn 0x10` read → ms byte. `fn 0x20(ms)` write.
  (`settings_templates.py:592-624`; libratbag `hidpp20.h:430-442` `HIDPP_PAGE_ADJUSTABLE_REPORT_RATE`).
- **EXTENDED_ADJUSTABLE_REPORT_RATE `0x8061`** (**the one on this mouse**):
  - `fn 0x10` caps → u16 flags, bit i enables index i of:
    `0=8ms(125 Hz), 1=4ms(250), 2=2ms(500), 3=1ms(1000), 4=500us(2000), 5=250us(4000), 6=125us(8000)`.
  - `fn 0x20` read → u8 current index.
  - `fn 0x30(index)` write.
  Source: `ExtendedReportRate` `settings_templates.py:625-653`; rate↔Hz table also documented
  at `docs/capabilities.md:227-237`. Device reports `1ms` stock; **8000 Hz needs `125us`**
  and `onboard_profiles=Disabled` (CLI doc `docs/PRO_X2_SUPERSTRIKE_CLI.md:59`, `:370`).
- Units trap: 0x8060 speaks milliseconds-as-index; 0x8061 speaks a 7-entry interval index —
  never mix. Rate requested > receiver's bandwidth silently falls back on some receivers; verify
  by timestamping motion reports (receiver c54d supports 8 kHz per marketing; kernel input path
  is the bottleneck — a custom driver should consume raw reports directly).

### 5.10 Config persistence model

Per `docs/capabilities.md:100-115`: almost all feature writes are volatile across power-save/off.
Persisted exceptions: ChangeHost, PERSISTENT_REMAPPABLE_ACTION, some fn-swap; HITS tuning
(section below) persists in device NVRAM *regardless* of onboard-profile state (`:203`,
CLI doc note 4 `:372`). Driver implication: re-push DPI/rate/buttons on every wake
(listen for wake/connect notifications rather than assuming).

### 5.11 ONBOARD_PROFILES `0x8100` (**present**) — full flash protocol

Descriptors: `hidpp20.py:1622-1728`; libratbag mirror `src/hidpp20.h:581,894-908`,
driver `src/driver-hidpp20.c`; Solaar user doc `docs/capabilities.md:239-326`.

- **Info** `fn 0x00` → 10 meaningful bytes (libratbag `struct hidpp20_onboard_profiles_info`,
  16 B struct): `{memoryModelId, profileFormatId, macroFormatId, profileCount, profileCountOob,
  buttonCount, sectorCount, sectorSize u16, mechanicalLayout/various, …}`;
  Solaar unpack `!BBB` → `memory(0x01=valid), currentProfile, _macro` then `!BBBBHB`
  → `count, oob, buttons, sectors, size, shift`; **g-buttons exist iff `shift & 0x3 == 0x2`**.
  Sanity gate Solaar uses: `memory != 0x01 or profile > 0x05` ⇒ bail (`:1665-1669`).
- **Modes** OnboardMode: 0 no-change, 1 onboard, 2 host (`hidpp20_constants.py:263-266`).
  Set `fn 0x10(mode)`, read `fn 0x20` (helpers `:2114-2123`). Solaar's richer wrapper also uses
  `fn 0x30(slot-data)` set-current-profile and `fn 0x40` get-active (`settings_templates.py:558-578`).
- **Headers** `fn 0x50(sectorHi, sectorLo, addrHi, addrLo)` → **16-byte memory window** reads.
  Sector 0 (RAM; if its first 4 B are `00000000`/`ffffffff`, read ROM sector 1) holds an array of
  `{profileSector u16BE, enabled u8, 0x00}` terminated by `FF FF` (pairs `get_profile_headers`,
  `:1638-1659`).
- **Profile record** (per-sector blob `size` bytes from Info; tail = CRC16-CCITT via
  `common.crc16`, `common.py:40`; pad 0xFF):
  | offset | size | field |
  |-------:|-----:|---|
  | 0 | 1 | report rate (ms) |
  | 1 | 1 | default resolution index (0-4) |
  | 2 | 1 | shift(DPI) resolution index |
  | 3 | 10 | 5 resolutions, **u16 LE** |
  | 13 | 3 | RGB (r,g,b) |
  | 16 | 1 | power mode |
  | 17 | 1 | angle snap |
  | 18 | 2 | write count, **u16 LE** (increment per save) |
  | 20 | 8 | reserved (FF) |
  | 28 | 2 | power-save timeout ms, u16 LE |
  | 30 | 2 | power-off timeout ms, u16 LE |
  | 32 | 64 | 16 button bindings ×4 B (normal mode) |
  | 96 | 64 | 16 g-shift bindings ×4 B |
  | 160 | 48 | name, UTF-16LE, NUL-padded (≈24 chars) |
  | 208 | 44 | 4 lighting-effect records ×11 B |
  | … | | FF filler to size-2, then CRC16 LE 2 B |
  Exact offsets: `OnboardProfile.from_bytes/to_bytes` `hidpp20.py:1548-1598`.
- **Button binding, 4 bytes** (`Button.from_bytes/to_bytes` `:1464-1519`; enums `:1404-1437`):
  `byte0` high nibble = behavior: **0** macro-execute, **1** macro-stop, 2 stop-all,
  **8** SEND, **9** FUNCTION. Macro → `sector 12-bit = (b0&0x0F)<<8 | b1`, `address u16 = b2,b3`.
  SEND → `b1`=mapping type (**0** none, **1** mouse-button as u16 bitmap b2b3, **2** modifier-mask b2
  + HID keycode b3, **3** consumer-code u16 b2b3); none ⇒ `FF FF`. FUNCTION → `b1`=function id
  (0 no-action, 1 tilt-left, 2 tilt-right, 3 next-DPI, 4 prev-DPI, 5 cycle-DPI, 6 default-DPI,
  7 shift-DPI, 8 next-profile, 9 prev-profile, A cycle-profile, **B G-shift**, C battery-status,
  **D profile-select** (b3=index), E mode-switch, **F host-button**, 0x10 scroll-down, 0x11 scroll-up),
  `b2`=FF, `b3`=data byte.
- **Lighting effect record, 11 bytes** (`LEDEffectSetting.from_bytes/to_bytes`, `:1287-1310`):
  `ID` (0x0 disable, 0x1 fixed color [+ramp], 0x2 pulse [color,speed-ms], 0x3 spectrum-cycle
  [period u16, intensity, form], 0x8 boot, 0x9 demo, 0xA breathe [color, period],
  0xB ripple [color, period], else raw), `color u24 RGB`, `intensity %` (0⇒default 100),
  `speed u8 ms`, `ramp` (0 default /1 up-down /2 none), `period u16 ms`, `form`, filler/bytes.
- **Write transaction** (`write_sector` `:1717-1728`): read-modify-write with CWP:
  1. `fn 0x60(sectorHi, sectorLo, 0, 0, lenHi, lenLo)` — prepare/erase target
  2. repeat `fn 0x70(16-byte chunk)` — data stream
  3. `fn 0x80` — commit (device recomputes CRC on load; header/meta CRCs required in record)
  Skip write when bytes equal (flash wear) — same policy libratbag applies via
  `hidpp20_onboard_profiles_write_sector` (`src/hidpp20.c:…`, decl `hidpp20.h:1002-1008`).
- Sectors ≥ 0xFF are ROM (read-only profiles, factory presets); write only RAM sectors
  (`docs/capabilities.md:252`, enforced `:1738-1739`).
- **Whole-suite YAML interchange**: Solaar `solaar profiles <dev> [file]` dumps/loads the full
  structure with `version: 3` envelope (`OnboardProfilesVersion` `:1618`).

### 5.12 MOUSE_BUTTON_SPY `0x8110` (**present**)

No OSS client code exists (Solaar/ratbag/logiops/kernel never call it — verified by grep).
Known from Logi Options+ era: device pushes per-button down/up/click notifications with
extended metadata (analog pressure/position for spy-aware controls) once enabled; enabling
typically = `fn 0x10(enable)` style. **Live-RE plan**: query caps `fn 0x00`, toggle suspected
enable functions while instrumenting hidraw. Until reversed, X2 buttons behave mechanically
through the standard motion/click reports and profile bindings.

### 5.13 ANALOG_BUTTONS / "SUPERSTRIKE TUNING" `0x1B0C` — THE headline feature

HITS = Hall-effect Inductive Trigger Switch primaries. Device-present V0.

Constants: `ANALOG_BUTTONS = 0x1B0C` (`hidpp20_constants.py:79`, commented "actuation point,
rapid trigger, haptics"); doc row `docs/features.md:53`; capability notes
`docs/capabilities.md:193-201`; live-device detail dump `docs/devices/PRO X 2 Superstrike 40BD.txt:70-101`;
implementation `settings_templates.py:4209-4385` (`_AnalogButton*RW` + `AnalogButtonTuning.build`).

Wire format (little-endian not needed; single bytes):

- **Caps** `fn 0x00` → `[0]=flags, [1]=buttonCount (reports 3; only indexes 0=L, 1=R reachable),
  [2]=maxActuation (wire), [3]=maxRapidTrigger (wire), [4]=maxHaptics (wire)`.
  On this unit: max wire values 40 / 20 / 20.
- **Read** `fn 0x20(buttonIndex)` → `[0]=index echo, [1]=actuation, [2]=rapidTrigger,
  [3]=haptics, …`.
- **Write** `fn 0x10(buttonIndex, actuation, rapidTrigger, haptics)` — **all four bytes, every
  time** (read-modify-write); no per-field function.
- **Quantization** — every tuning byte is `wire = logical << 2` (6-bit logical in bits 7..2;
  low bits reserved-zero). Logical universes:
  - actuation: 1-10 (wire 4-40, multiple of 4); 1 = hair trigger, 10 = deepest; default 5
  - rapid trigger: 1-5 (wire 1-20 … wire unit reported as low 4..20 in dump; keep ×4 mapping);
    1 = most sensitive; **cannot be disabled**; default 3
  - haptics: 0-5 (wire 0-20, multiples of 4); 0 = off; default 3
  (`:4308-4315` caps decode `caps[2]>>2`; RW shift `:4220, 4246-4249, 4267-4268`).
- **Bit 0 of byte 2 (rapid-trigger field) is a firmware-managed sensitivity flag — PRESERVE it**
  on write: `wire_rt = ((logical & 0x3F) << 2) | (current[2] & 0x01)` (`:4249`). Dropping
  reserved bits triggers `INVALID_ARGUMENT` on write — regression filed as Solaar issue #3202
  (comment at `:4201-4207`).
- **Defaults** for blind probes: read mid-values `actuation 0x14`(wire)/5, rt 3, haptics 3.
- **Persistence**: HITS values survive reconnects and are orthogonal to onboard profiles
  (dump `:372`; capabilities `:203`).
- Legacy-value migration footnote: Solaar 1.1.19 persisted wire (×4) values and later migrates
  them (`_AnalogButtonSetting._pre_read` `:4281-4309`) — a driver should expect stale configs
  in the wild.
- Related, same family: **FORCE_SENSING_BUTTON `0x19C0`** — per-button force thresholds:
  `fn 0x00` count; `fn 0x10(n)` → `!HHHH` {flags bit0 changeable, default, max, min};
  `fn 0x20(n)` → u16 current force; `fn 0x30(n u8, force u16BE)` set (`ForceSensingButton`
  `hidpp20.py:2321-2357`). **HAPTIC `0x19B0`** — MX-Master-4 style waveforms, enable/intensity
  via `fn 0x10/0x20` + `fn 0x40(waveform-id)` plays (`HapticLevel/PlayHapticWaveForm`
  `settings_templates.py:4376-4450`; waveform table `hidpp20_constants.py:368-385`).
  Not enumerated on 40BD, but 0x80E0-stub note implies haptics run through 0x1B0C byte 3 here.

### 5.14 Reprogrammable-keys family (reference — NOT enumerated on 40BD)

`REPROG_CONTROLS_V4 0x1B04` CID machinery (task/derive/index/group/gmask; divert/persistent-divert/rawXY
reporting; remap via `_setCidReporting`) `hidpp20.py:445-613`, kernel implementation
`kernel:3622-3756`; `PERSISTENT_REMAPPABLE_ACTION 0x1C00` persistent action slots `:615-679`,
setting `settings_templates.py:1519-1577`. X2 relies on profile bindings + spy instead — keep
these in the toolkit for detection and for future firmware.

### 5.15 Lighting stack (reference — NOT enumerated on 40BD)

`COLOR_LED_EFFECTS 0x8070` zone table + effect set (get zones `fn 0x00/0x10/0x20`, set effect,
onboard/host toggle `LEDControl`), `RGB_EFFECTS 0x8071` extended variants incl. idle/effect
timeout + boot/shutdown animation (`settings_templates.py:3204-3741`), `PER_KEY_LIGHTING_V2 0x8081`
per-key maps (`:3845-4159`), `BRIGHTNESS_CONTROL 0x8040` (`:3132-3175`). Enumerated only in
`hidpp20_constants.py` for the Superstrike: nothing — corroborates lighting autonomy via profiles.

---

## 6. Receiver-side and pairing facts

- `0xC54D` registered by Solaar as a **Lightspeed receiver** with the standard
  `_lightspeed_receiver` descriptor set (pairable, unpairable semantics per class) —
  `base_usb.py:168-179, 201-223`.
- Device WPID over-the-air on Lightspeed receivers = `40BD` (`docs/devices.md:214`).
- Wired USB alternative transport implied by `usbid: C0A8` transport slice of Model ID
  (`40BDC0A80000`, dump `:22`) — feature-call identical, reports often allow 20-byte long frames.
- HID++ 4.2 ≙ "HID++ 2.0 extensions, gen-4 gaming tier": new-gen features observed
  (`0x2202`, `0x8061`, `0x1B0C`, `0x8090`, `0x8110`) coexist with baseline 0x0003/0x0005 V7/V5.
- Bolt-authentication does NOT apply to Lightspeed receivers (pairing auth is receiver-class-
  specific, `docs/capabilities.md:70-83`).

## 7. Implementation checklist for the Linux driver

1. **Transport**: hidraw on receiver; frames §2; enumerate features via §3 with the §4 map
   preloaded (indices differ per pairing slot — always rediscover at runtime, use the map as
   validation).
2. **Probe order**: ping → ROOT::GetFeature for {0x2202, 0x8061, 0x1B0C, 0x8100, 0x1004, 0x8110,
   0x8090} → read caps of each → settle host-mode: set onboard `mode=0x02(host)` or expose both.
3. **Motion path**: rely on kernel mouse reports initially; consider diverting to raw reports
   at 8 kHz only after measuring kernel latency; verify `125us` took effect via timestamps.
4. **DPI**: read tables via §5.4 grammar, cache stream; write triplet X/Y/LOD atomically (fn60).
5. **HITS**: RMW with flag preservation (§5.13); expose logical 1-10/1-5/0-5; debounce writes;
   read-back after write mandatory (no ACK semantics beyond echo).
6. **Profiles**: read-only viewer first (headers + sector decode §5.11); enable writes only
   behind an explicit user opt-in (CRC+clobber risk acknowledged by Solaar docs `:324-326`).
7. **Battery**: poll 0x1004 fn10; subscribe to status-event notification; map to UPower.
8. **Spy/MODE_STATUS/hidden set**: instrumentation phase — log all short/long notifications
   while clicking/scrolling/switching profiles; correlate byte layouts; iterate on `0x9403`.
9. **Danger zones**: cookie 0x0000 side-effect (§3 CONFIG_CHANGE), flash-commit without CRC,
   dropping rt-flag bit, sending 0x8060-style ms-index to 0x8061.

## 8. Pointers

- Feature enum + constants: `refs/Solaar/lib/logitech_receiver/hidpp20_constants.py`
- Layouts/decoders: `refs/Solaar/lib/logitech_receiver/hidpp20.py`
- Settings/fn-ids: `refs/Solaar/lib/logitech_receiver/settings_templates.py`
- Device dump: `refs/Solaar/docs/devices/PRO X 2 Superstrike 40BD.txt`
- CLI exposure: `refs/Solaar/docs/PRO_X2_SUPERSTRIKE_CLI.md`
- Alternative C implementations: `refs/libratbag/src/hidpp20.{c,h}`, `refs/kernel/hid-logitech-hidpp.c`
- Legacy enum cross-check: `refs/logiops/src/logid/backend/hidpp20/feature_defs.h`
