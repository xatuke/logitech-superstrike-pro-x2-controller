# Community prior art: PRO X 2 Superstrike, c54d Lightspeed, comparable mice

Compiled 2026-10-07. Every claim below is sourced from public code/issues/docs or
from artifacts cloned/downloaded into
`downloads/community-corpus/` (verbatim, readable, not executed). Structured
pointers to the local copies are in §8. URLs in parentheses are the canonical
sources.

The headline: **four independent community implementations of this exact mouse
already exist** (Solaar PR #3132, mclol0/linux-superstrike in Go,
kazehana99k/superstrike-hits in Python with G HUB USBPcap captures,
zeex64/openghub in Go/Wails) plus one vendor-binary RE effort that recovered all
eight onboard-profile formats straight out of Logitech's own Onboard Memory
Manager DLL (OpenMouse-Project/mouse-protocol). Together they form a complete,
mutually cross-checked picture of the device's HID++ surface. Between them they
agree on ~everything, disagree in two places (see §7 disagreements), and encode
the operational traps that bite live drivers.

---

## 1. Device identity, both transports (verified 3 ways)

| Property | LIGHTSPEED link | Wired USB |
|---|---|---|
| Receiver/endpoints | `046d:c54d` "USB Receiver", kernel name `Logitech lightspeed receiver` | mouse as own USB device `046d:c0a8` |
| WPID / PID on link | WPID **40BD** (receiver pairing info + HID_ID `0003:0000046D:000040BD`) | USB PID **C0A8** |
| Product string | `Logitech X2 SUPERSTRIKE` (dj child node) | `Logitech PRO X2 SUPERSTRIKE` |
| Marketing name via feature 0x0005 | `PRO X2 SUPERSTRIIKE` (Logitech's typo, three I's) | same |
| Model ID | `40BDC0A80000` (WPID ++ USB id ++ zeros) | — |
| HID++ version | 4.2 | 4.2 |
| Firmware on one unit | BL2 73.00.B0011, MPM 42.00.B0011 (receiver fw 07.02.B0011) | — |

Sources: Solaar issue #3202 live `solaar show` (github.com/pwr-Solaar/Solaar/issues/3202),
mclol0/linux-superstrike REVERSE_ENGINEERING.md + issue #4 scan output, Solaar
`docs/devices.md` ("PRO X 2 Superstrike 40BD 4.2"), kazehana PROTOCOL.md §1,
mouse-protocol `docs/logitech-testing.md` (c0a8 = PRO X 2 Superstrike USB).

Comparable mice WPID/USB table (same family, same receiver class):

| Device | WPID on receiver | Wired USB PID | HID++ | Notes |
|---|---|---|---|---|
| G Pro X Superlight (gen1) | 4093 | c094 (per mouse-protocol; OpenRGB legacy table uses 0x4079=G Pro Wireless) | 4.2 | onboard format **4** (base v1) — real dump in mouse-protocol tests |
| **Pro X Superlight 2** | **40A9** (NOT 40A2) | **c09b** | 4.2 | onboard format **7** (base v6 + bunny hop); "Profile layout not supported: 0x06" in libratbag (#1519) |
| **PRO X 2 Superstrike** | 40BD | **c0a8** | 4.2 | onboard format **8** (base v6 + bunny hop + analog buttons) |
| **PRO X 3 Superstrike** | — | **c0a9** | 4.2 | same format 8; 0x2202 list 100–48000; 8 kHz WORKS over its cable (0x8061 mask 0x7f) — mouse-protocol doc |
| G604 | 4085 | — | 4.2 | Solaar docs/devices.md |
| G900/G903/G703/G403/GPW/G502LS | 4053/4067/4070/405D/4079/407F | c08x… | 2.0-era | OpenRGB legacy wireless-PID map |

Receiver family (kernel `hid-logitech-dj.c` + mouse-protocol):
c539 (HERO-era), c53a/c53d/c53f/c541 (Lightspeed/Nano LS 1.1), c543 (LS 1.2, G305),
c547, **c54d (LS "1_4" type)**, c545 — all `recvr_type_gaming_hidpp[_ls_1_3]`;
c548 = Bolt (different pairing protocol, BOLT subregister 0x50).
The c54d exposes **3 USB interfaces** (`no_dj_interfaces = 3`), reports **max 2
paired devices** in the field (`out of a maximum of 2` in two `solaar show`
dumps), and its paired-device packets are **13-byte mouse reports** (kernel
comment at hid-logitech-dj.c:1809).

## 2. Addressing through the c54d receiver — the exact conventions that work

This is the part the task flagged as critical. Four independent working
codebases agree:

1. **Requests to the paired device use device index = slot number, i.e. `0x01`
   for the first paired device — NOT `0xFF`.** Evidence: raw Solaar trace
   `[11 01 0C23 ...]` (issue #3202); kazehana §1 "Device index: wireless = 1
   (single paired device)"; linux-superstrike README "device index 0x01 (not
   0xFF)"; OpenRGB drives receiver slots with `slot.index` taken from pairing
   enumeration (LogitechControllerDetect.cpp:1399). `0xFF` is for the receiver
   itself (RAP register access) and for DIRECTLY connected (cable) devices.
2. **Slot number comes from the receiver's pairing machinery, not guesswork:**
   - Solaar: hidpp10 register read `0x83B5` (long reg 0xB5) with sub
     `0x20+n-1` (pairing info: WPID at bytes 3:5), `0x30+n-1` (extended: serial
     at bytes 1:5), `0x40+n-1` (codename); count = reg `0x02` read (response
     byte 1); max devices = reg `0xB5` sub 0x03 response byte 6; enumerate by
     looping slot 1..7. (lib/logitech_receiver/receiver.py, hidpp10.py)
   - OpenRGB variant of the same: enable wireless notifications (write reg
     `0x00` bit0), read reg 0x02, then **fake reconnect** by writing `0x02` to
     reg 0x02 and consuming the DJ connect notifications: byte[1:2] = virtual
     PID (BE), device_index byte = slot, `data[0]&0x40` = paired-but-asleep
     flag (LogitechProtocolCommon.cpp `getWirelessDevice`). Both paths work;
     the register path answers even when the mouse is asleep.
   - Wired slot mapping on the node itself: paired slot n ↔ hidraw child whose
     HID device `phys` == `<receiver phys>:n` (Solaar
     `lib/hidapi/udev_impl.py find_paired_node`) — that is the kernel's
     hid-logitech-dj rule (`DJ_DEVICE_INDEX_MIN = 1`).
3. **Frame**: `[report_id][device_index][feature_index][(function<<4)|sw_id]`
   report_id 0x10 short (7B) / 0x11 long (20B); long reports are always safe to
   send on this device (Go impl sends 0x11 exclusively; the receiver's control
   interface is USB MI_02, usage page 0xFF00 — kazehana §1, OpenRGB "FAP Short
   Message usage 1 / long usage 2" for Bolt-style nodes).
   `sw_id` is an arbitrary sender tag in 1..15 that replies echo for matching.
   Observed in the wild: G HUB `0x1d` (fn1,sw=D), Solaar wire traces sw=3/6 and
   fixed `0x0B` in master (SOLAAR_SOFTWARE_ID, base.py:910), OpenRGB `0x07`
   (HIDPP20_SW_ID), Go app `0x0A`. Any value works; match replies on it.
4. **Errors**: reply has byte2 `0xFF` (v1-style, seen live:
   `[11 01 FF 0C 16 02...]` — INVALID_ARGUMENT 2 on a bad 1B0C write) or
   `0x8F` (v2-style sentinel the Go impl also handles). Error code meanings:
   01 unknown, 02 invalid_argument, 03 out_of_range, 05 logi_internal,
   06 invalid_feature_index, 07 invalid_function, 08 busy, 09 unsupported
   (linux-superstrike hidpp.go errName).
5. **Feature index ≠ feature id.** The device's FeatureSet (feature 0x0001) maps
   runtime indices to 16-bit IDs; both G HUB and community tools resolve IDs at
   runtime (kazehana resolves via `IRoot.getFeature(0x1B0C)` before any use;
   OpenMouse/OpenRGB enumerate via FeatureSet getCount/getFeatureID — FuncSw
   0x23/0x11 seen in captures). On
   current firmware the stable observed index map is in §4 (1B0C = index 0x0C)
   but the docs themselves warn: never hardcode, always resolve.
6. **A pragmatic brute force that demonstrably works for discovery**: walk every
   `/dev/hidraw*` whose HID_ID contains 046d, ping indices
   `0x01, 0xFF, 0x02..0x06` with a 350 ms timeout, first responder wins
   (linux-superstrike device.go). Wired caveat: on c0a8 THREE indices answered
   simultaneously (0x01, 0x02, 0xFF) — issue #4 scan log; pick deterministically.

## 3. Kernel-side facts (torvalds/linux drivers/hid)

- `hid-ids.h`: `USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1_4 = 0xc54d`
  (NANO_LIGHTSPEED_1_1 = c539, 1_2 = c543, 1_3 = c547).
- `hid-logitech-dj.c` table entry "Logitech lightspeed receiver (0xc54d)" →
  `recvr_type_gaming_hidpp_ls_1_3`. Shares behavior with c547/c545: LED
  passthrough uses report id 1; synthetic high-res mouse descriptor
  `mse_high_res_ls_1_3_descriptor` (16 buttons, 16-bit X/Y ±32767, 8-bit wheel +
  AC pan, report id 2) is handed to userspace for the paired mouse.
- Paired child nodes are 0x40XX PIDs (40BD here) on the `hid` bus;
  OpenRGB explicitly **skips** the dj child nodes and drives the receiver's own
  interface so the same device isn't driven from two handles
  (LogitechControllerDetect.cpp "skipping DJ virtual node").
- No Superstrike-specific quirk exists in the kernel (grep 40BD/Superstrike = 0
  hits); everything device-specific rides HID++ from userspace. `hidpp`
  driver only adds hi-res wheel bits for wired legacy devices.
- Consequence for us: userspace driver must claim the receiver's control
  hidraw AND/OR the dj child — never both blindly — and must coexist with
  evdev input from the child node.

## 4. The device's HID++ 2.0 feature table (two live dumps agree)

From Solaar `docs/devices/PRO X 2 Superstrike 40BD.txt` (36 entries incl. tip
dual-boot variants) and kazehana `feature-table.txt` (indices identical):

```
0x00 ROOT. 0x01 FEATURE SET. 0x02 FW VERSION (0003, V7). 0x03 DEVICE NAME (0005,V5)
0x04 WIRELESS DEVICE STATUS 1D4B. 0x05 CONFIG CHANGE 0020. 0x06 UNIFIED BATTERY 1004 V5
0x07 XY_STATS 2250 (MouseTuning: gaming surface/LightForce). 0x08 WHEEL_STATS 2251
0x09 EXTENDED ADJUSTABLE DPI 2202 V0. 0x0a MODE STATUS 8090 V3
0x0b (stub, per Solaar) / BUNNY HOPPING 80E0 (per openghub capture)
0x0c ANALOG BUTTONS 1B0C  <-- HITS
0x0d EXTENDED ADJUSTABLE REPORT RATE 8061 V0. 0x0e ONBOARD PROFILES 8100 V0
0x0f MOUSE BUTTON SPY 8110. 0x10 FORCE PAIRING 1500
0x11-0x17: 1801, 1802 DEVICE RESET, 1803, 1806 CONFIG DEVICE PROPS V8, 1817, 1805 OOBSTATE
0x18-0x23: 1830 (power modes per kazehana), 1877, 9403, 1861, 1890, 18A1,
           1E00 (toggle "hidden features"), 1E02, 1E22, 1E30, 1602, 1EB0, 18B1
Absent: 0x2201 classic DPI, 0x8060 classic rate, 0x807x RGB (no lighting),
0x1B04 ReprogControls (no live button remap — via profile only).
```

Haptics/rumble hardware exists (vibration motor for click feel) but the only
exposed knob is 1B0C byte3; Solaar's generic `HAPTIC` feature (`haptic-play`
waveforms) is NOT in this device's table.

## 5. Feature-by-feature wire protocol (all capture- or cross-project-verified)

### 0x1B0C ANALOG BUTTONS (HITS) — the headline
- `fn0` GetCaps → `[00, nButtons=03, actMax, rtMax|hapMax, hapMax, flags01]`;
  observed `00 03 28 14 14 01` (act 0x28=40=10lvls×4, then two 0x14=20=5lvls×4).
  Both Solaar and kazehana read caps[2]=actuation 40, caps[3]/caps[4]=20 (RT and
  haptics; the two dumps interpret order slightly differently — harmless, values equal).
- `fn2` GetCfg per button → `[btn, act<<2, rt<<2 | sensFlag, hap<<2]`
- `fn1` SetCfg per button, same layout, echo reply.
  Wire units: **every value is logical<<2** in bits 7..2, low bits reserved;
  RT byte **bit0 = firmware sensitivity flag, read-and-write-back**
  (else INVALID_ARGUMENT — Solaar PR #3207 fixing #3202). Valid quanta:
  act 0x04–0x28 (levels 1–10), RT 0x04–0x14 (1–5), haptics 0x00–0x14 (levels 0–5
  Solaar / "1–6" kazehana UX = (lvl−1)×4 — same bytes).
- G HUB ground-truth sample from USBPcap: `11 01 0c 1d 00 0c 09 14` =
  left, act level3 (0x0c), RT 2+flag (0x09), haptics max (0x14).
- Applies live, no profile reload. Buttons: 0=left 1=right; caps say 3 buttons
  but only 0/1 are reachable.
- **Power-on source of truth is the profile** (§6 analog block); live writes are
  the working set. Solaar's docs claiming "persists across reconnections" hold
  only because profiles reload them (mouse-protocol doc §"HITS settings are
  stored in the profile").

### 0x2202 EXTENDED ADJUSTABLE DPI
- `fn2` getDpiList paged: params `[sensor=0, dir, page]`, data from reply byte 3,
  16-bit BE values, terminator 0x0000, `val>>13==0b111` = step marker
  (step 13 bits + next-word last) — produce_dpi_list algorithm identical in
  Solaar settings_templates.py:1035 and Go app.
- `fn5` get `[sensor]` → DPI BE at bytes 1:3 (+Y at 5:7 if separate, LOD at 9).
- `fn6` set `[sensor, Xhi, Xlo, Yhi, Ylo, lod]`; validator prefers 1-byte index?
  No — writes raw words; OpenMouse settled LOD encoding = 1..3 (Low/Med/High),
  **0 = "stage unused / no lift-off control", NOT a level** (off-by-one saga).
- **Critical device quirk: this live setter is a firmware NO-OP on the
  Superstrike** (measured DPI ratio 1.0 at 400 vs 1600 in linux-superstrike §DPI;
  openghub concurs: "the device runs entirely from its onboard profile sector").
  Getter also lies (stale). DPI is persisted/applied only through the profile.

### 0x8061 EXTENDED ADJUSTABLE REPORT RATE
- `fn1` list → 16-bit mask; bit i ⇒ index i = `[125,250,500,1000,2000,4000,8000] Hz`
- `fn2` get → index (STALE after writes on this device).
- `fn3` set → index. **Works live in host mode** (Go team measured 998 Hz after
  setting 1000) but is not persistent; persistent value = profile rate byte.
- 8 kHz IS offered over LIGHTSPEED (mask covers index 6; Solaar CLI doc lists
  125 µs). Wired c0a8 caps at 1 kHz, sibling c0a9 at 8 kHz (mouse-protocol).

### 0x8090 MODE STATUS (gaming surface + LIGHTFORCE switch mode)
- The industry term "LIGHTFORCE" maps here: OpenMouse testing doc: "gaming-
  surface and Lightforce modes use 0x8090" (validated first on G502X family).
- openghub special.go, captured from G HUB: get `fn0` `[sensor]` →
  `[sensor, mode]`; set `fn1` `[00, mode, 00, 06]`; modes Auto=0x00, On=0x02,
  Off=0x04 (getter may transiently say 0x01 for On; treat as On).

### 0x80E0 BUNNY HOPPING (scroll filter, not a "haptics stub")
- openghub (capture-based): `fn1` read window → wire = ms÷10; `fn2` write wire;
  0 = off, valid 10..100 ⇒ 100–1000 ms in 10 ms steps. Same byte also lives in
  the profile at 0x25 for format 7/8 (below). Solaar's doc guessed 9403 was
  "hidden BHOP" — openghub's identification is the backed one; 0x9403 remains
  unidentified.

### 0x8110 MOUSE BUTTON SPY — present; community tools don't consume it; Solaar
lists it (button-event notifications for diversion-style behavior).
### 0x1830 — kazehana annotates "power modes"; Solaar MODE_STATUS-equivalent
treatment on other devices; treat as power-scheme toggle (untested publicly).
### 0x1004 UNIFIED BATTERY: `fn1` getStatus → resp[0]=%, resp[2]=charging.

## 6. Onboard profiles (0x8100) — format 8, full recovered layout

Triple-sourced: linux-superstrike REVERSE_ENGINEERING.md (probed on device),
OpenMouse mouse-protocol `docs/logitech-onboard-profiles.md` (decompiled
Onboard Memory Manager 2.6.1749 `logi_nethidppio.dll`, all 8 formats + caps),
openghub profile.go (implements it defensively). Dispatch table (vendor):
format 8 = base v6 + bunny hopping + analog buttons = Superstrike/X3.

- `fn0` getInfo → `[memoryModel=1, profileFmt=8, macro, count=5, oob, buttons,
  sectors, size=255 BE16, shift]` → **5 profiles, sector size 255, 5 user buttons**.
- Mode: `fn1` set `01`=onboard / `02`=host; `fn2` get.
- Current profile: `fn3` set `[secHi,secLo]`, `fn4` get. **Activation can TIME
  OUT and must be retried** (mclol0 issue #4); reload happens on activation,
  which is also how rate/DPI become visible.
- Memory IO: `fn5` read `[secHi,secLo,offHi,offLo]`→16 B; `fn6` begin
  `[sec, 00 00, len BE16]`; `fn7` write 16 B; `fn8` commit.
  Whole-sector writes only (flash erase per save); writes often ACK-less —
  use ~100 ms ack timeout (openghub profile.go) and VERIFY by read-back+CRC,
  retrying transient busy reads.
- Directory (RAM sector 0x0000): 4-byte entries `[sectorHi,sectorLo,flag,pad]`
  terminated 0xFFFFFFFF. **byte2 = active marker** (mouse-protocol, hw-verified
  directory dump `00 01 01 ff | 00 02 00 ff ...` CRC 0x4037) — the two Go apps
  read it as "enabled"; vendor code says it flags the active profile.
- Factory/ROM sectors (0x0101+) store `FF FF` where RAM has the CRC; read-only,
  CRC check must be waived (issue #1). **Pristine (never G-HUB'd) units have an
  empty RAM directory** — bootstrap = copy ROM 0x0101 → RAM 0x0001, patch,
  valid CRC, write minimal control table, setCurrentProfile (exact recipe in
  issue #1, verified working from pure Linux).
- Format 8 profile sector map (vendor-recovered; matches two field decoders):

| Offset | Content |
|---|---|
| 0x00 | report rate **wireless** (index into [125..8000], = 125<<idx) |
| 0x01 | report rate **wired** (same encoding) |
| 0x02 | dpi_v6: default stage idx |
| 0x03 | g-shift stage idx |
| 0x04+5n (n=0..4) | stage n: `[Xlo,Xhi,Ylo,Yhi,LOD]` **little-endian**, LOD 0=unused,1=Low,2=Medium,3=High (format 8 offers Low/High per mouse-protocol) |
| 0x1d | dpi_delta (4 B) |
| 0x21 | power_mode; 0x22 angle_snapping; 0x23 write_counter (wear odometer) |
| 0x25 | bunny_hopping window (ms÷10) |
| 0x26 | **analog_button 6 B = 2×3 B per primary button `[act<<2, rt<<2|on, hap<<2]`** (HITS power-on defaults; factory image `14 08 0c ×2`, CRC 0x2a38) |
| 0x2c/0x2e | power-save / power-off timeouts |
| 0x30 (48) .. 0x6f | button_functions: 16×4 B |
| 0x70 .. 0x9f | g-shift button table |
| 0xa0 (160) .. 0xcf | profile name UTF-16LE, 24 code units |
| 0xd0..0xfb | lighting clusters (absent semantics on this RGB-less mouse) |
| 0xfc | lighting_flag |
| 253..254 | **CRC-16/CCITT-FALSE** (poly 0x1021, init 0xFFFF, no reflect/xor), BE |

- **Reconciliation of the two published "profiles" articles** (§7): the
  linux-superstrike write-up ("5 uint16 LE DPI words at 3..12; X/Y are last two;
  buttons at 48") describes a G-HUB-CONFIGURED unit's legacy-flavored decode;
  mouse-protocol + issue #1's pristine dump prove the true v6 geometry
  (stages at 0x04..). Same bytes, different interpretation; the vendor layout is
  authoritative and explains both readings (word-aliasing of stages).
- Button entry (profile, 4 B): byte0 hi-nibble behavior `0x8` SEND (type 01
  mouse-mask bit list L=1,R=2,M=4,B=8,F=0x10,DPI=0x2000 / 02 modifier+keycode /
  03 consumer / 00 no-action 0xFFFF), `0x9` FUNCTION (3 next-DPI, 5 cycle-DPI,
  8 next-profile, 0xA cycle-profile, 0xB G-shift...). Factory slots: L R M Back Fwd.
  Vendor opcode enum (FUNCTION_OPCODES 0..17 incl TILT, G_SHIFT 11, TOGGLE_
  PERFORMANCE_MODE 14) in mouse-protocol §enums.

## 7. Operational gotchas observed in the wild (steal these conclusions)

1. **Stale getters are systemic** on this device: 0x2202 get, 0x8061 get, mode
   status read-backs. Verify writes by BEHAVIOR (measure report gaps via second
   hidraw reader or EV_SYN deltas; DPI via summed |Δx|+|Δy| ratios).
2. **Live vs profile duality**: live feature writes are volatile and partially
   no-op (DPI); the profile sector is the persistence; profile ACTIVATION is
   the reload hook and can time out (retry, minutes-scale patience).
3. Flash wear: never automate sector writes; diff-before-write; single staged
   write per save (mouse-protocol "rules for any write path").
4. Pristine units: empty RAM directory, ROM-only profiles, no CRC in ROM (§6).
5. The mouse claims 36 features but several (9403, 18x1 clusters, 1E2x) are
   internal; 0x1E00 is suspected "enable hidden features" (both dumps annotate).
6. G HUB coexistence: it holds the vendor interface; the mouse may stop
   answering while G HUB runs (mouse-protocol testing checklist).
7. Dual radio bindings exist (Solaar #3202 shows model with on-disk RT config
   differing per boot; OpenMouse warns onboard/host mode flips when G HUB
   launches) — reapply settings on HID++ CONFIG CHANGE (0020) notifications.
8. OpenMouse capture of rapid-trigger field `x=0x09` etc. underscores: preserve
   unknown/reserved bits on EVERY RMW (cheap insurance policy).
9. libratbag state: PXSL2 blocked at "Profile layout not supported: 0x06"
   (#1519) — Superstrike (format 8) necessarily also unsupported there; don't
   count on ratbag as prior art beyond hidpp20 boilerplate.

Known issues worth reading in full (all zipped context = deltas over the above):
- Solaar #3202 + PR #3207 (wire encoding, raw traces)
- Solaar #3287 (RT slider "doesn't do anything" on CachyOS — likely live value
  being shadowed by profile reload; unfixed as of clone date)
- Solaar #3252 (gap list: profiles UI, Hz selector, default-DPI, gshift)
- mclol0 #1 (pristine ROM bootstrap) and #4 (activation timeouts; wired multi-index)
- libratbag #1519/#1572/#1780/#PR1844 (family support state, G Pro X 2 Dex)

## 8. Local corpus (cloned/fetched under `downloads/community-corpus/`)

| Path | What | Canonical URL |
|---|---|---|
| `solaar/` | Solaar master (incl. PR #3132 merged: descriptors, settings_templates 0x1B0C impl, docs/PRO_X2_SUPERSTRIKE_CLI.md, docs/devices 40BD.txt) | github.com/pwr-Solaar/Solaar |
| `linux-superstrike/` | Go driver+GUI; REVERSE_ENGINEERING.md = best single narrative | github.com/mclol0/linux-superstrike |
| `Logitech-PRO-X2-SUPERSTRIKE-...-/` | Python CLI; PROTOCOL.md = G HUB USBPcap-grounded 1B0C; feature-table.txt | github.com/kazehana99k/Logitech-PRO-X2-SUPERSTRIKE-Linux-haptics-actuation-control- |
| `openghub/` | Go/Wails app; special.go = 0x8090/0x80E0 captures; profile.go defensive impl | github.com/zeex64/openghub |
| `mouse-protocol/` | OpenMouse-Project protocol RE; onboard-profiles.md = vendor-DLL layout bible; testing matrix | github.com/OpenMouse-Project/mouse-protocol |
| `linux/` | torvalds/linux shallow: hid-logitech-dj.c (c54d, ls_1_3, descriptors), hid-ids.h | github.com/torvalds/linux |
| `logiops/` | LogiOps: feature_defs, ReceiverMonitor (indexing baseline) | github.com/PixlOne/LogiOps |
| `openrgb-hidpp20/` | OpenRGB LogitechHIDPP20 + Detector + ProtocolCommon: receiver enumeration, virtual-PID skip, 0x8070/71 RGB fns | github.com/CalcProgrammer1/OpenRGB |

Greppable quick keys for the corpus: `grep -rn "1B0C\|40BD\|c54d\|8061\|8090\|80E0"
downloads/community-corpus/{solaar,linux-superstrike,openghub,mouse-protocol}/`

## 9. Confidence ledger

- Verified (multiple independent HW sources): device identity/WPIDs; device
  index 1 through c54d; 1B0C layout+quanta+sensFlag; 8061 fn map+index order;
  2202 paging+LE-profile-DPI+dead-live-setter; 8100 fn set, 255-byte sectors,
  format 8 map, CRC-16/CCITT-FALSE; activation timeouts; receiver reg/dj
  enumeration conventions; kernel c54d classification.
- Likely (strong single source / inferred): LIGHTFORCE term↔0x8090 mapping;
  0x1830 = power modes; 0x1E00 hidden-feature toggle; pristine-cause split of
  legacy vs v6 profile decodes; receiver max-2 capacity (two solaar dumps, not 6).
- Uncertain/contested: 9403 identity; whether directory byte2 means "enabled"
  (two Go apps) or "active" (vendor model) — likely BOTH fields are conflated in
  different sectors' contexts, verify on HW; exact caps[3]/caps[4] semantic
  order in 1B0C fn0; UART of 18xx cluster features.
