# Logitech G PRO X2 SUPERSTRIKE — reverse-engineered protocol (046d:40bd)

Reverse engineered 2026-10-07 against live hardware (unit serial `59-1b-2f-d5`,
model string `2621LVL73V98`, fw id `MPMB`, bootloader `BL2`), with triple
cross-validation against Solaar 1.1.20 (PR #3132/#3207), libratbag PR #1896,
linux-superstrike / openghub / OpenMouse mouse-protocol, and G HUB USBPcap
captures quoted in the community corpus. Where sources disagreed, live
measurement won. Credits and links: `../research/`.

## 0. TL;DR for implementers

```
transport : HID++ 4.2, hidraw, report ids 0x10 (7 B) / 0x11 (20 B)
frame     : [rid][devIndex][featureIndex][(func<<4)|swid][params…]
address   : devIndex = the receiver slot (0x01 here; hid-logitech-dj rewrites
            it on the child hidraw node anyway), 0xFF when wired
errors    : [rid][devIndex][0xFF][featIdx][fn|swid][code] — devIndex is NOT
            changed; byte 2 is 0xFF. Receiver-side HID++ 1.0 errors use 0x8F
            in byte 2. (Before 2026-10-07 the driver expected devIndex 0xFF,
            so every error degraded into a timeout.)
swid      : use 2..15. swid 0 marks notifications — the kernel hidpp driver
            parses a swid-0 *reply* as an event (our early probes injected a
            fake "15 % charging" battery event that way); swid 1 is the
            kernel's own.
discovery : never hardcode feature indexes — resolve 16-bit feature ids via
            Root(0x0000) fn0, and walk FeatureSet(0x0001) (count fn0, entries
            fn1 → [fid_hi][fid_lo][kind][version], ordinal == index)
```

Firmware answer etiquette learned the hard way:

- Requests with >3 params MUST use the long report (0x11). Short frames with
  long payloads are silently dropped or rejected `invalid_argument`.
- Every feature call may time out transiently; retry once after ~30 ms
  (radio wake-up) — onboard-profile IO may need 4 retries at ~120 ms.
- Stale getters are systemic (DPI, rate, mode-status read-backs lag). Verify
  writes by read-back AND behavior, never by getter alone.

## 1. Device identity

| Thing | Value |
|---|---|
| WPID over receiver | `0x40BD`, device name `Logitech X2 SUPERSTRIKE` |
| Wired USB PID | `0xC0A8` (from Model ID `40BDC0A80000`) |
| Receiver | `046d:c54d` “USB Receiver” (Lightspeed 1_4), 3 interfaces, ≤2 devices |
| Feature 0x0005 name | `PRO X2 SUPERSTRIKE` (18 bytes) |
| DeviceInfo (0x0003 v7) | fn0 → `[entities=02][unitId 59 1b 2f d5][transport 00 0c][modelId 40bd c0a8 0000][extModel 01][caps 01]`; fn1 `[entity]` → `[type][prefix 3 ASCII][number BCD][revision BCD][build BE16]…`: entity 0 = bootloader `BL2 73.00.B0011`, entity 1 = main `MPM 42.00.B0011` (the old “MPMB” reading was prefix + BCD 0x42); fn2 → ASCII model `2621LVL73V98` |
| HID++ protocol | 4.2 (Root.Ping reply `[04][02]`) |
| Official spec | HERO 2 sensor, 100–44,000 DPI marketing ceiling, 888 IPS / 88 G, 61 g, 90 h battery, wired 1000 Hz / LIGHTSPEED 8000 Hz, SKU 910-007700 |

## 2. Feature table (live-validated indexes; re-resolve at runtime!)

| Idx | Fid | Meaning (verified unless noted) |
|----|------|--------------------------------|
| 0x00 | 0x0000 | Root: fn0 get-feature-idx, fn1 ping, fn3 get-protocol-version |
| 0x01 | 0x0001 | FeatureSet v2 |
| 0x02 | 0x0003 | DeviceInfo v7 (above) |
| 0x03 | 0x0005 | DeviceName v5 (len fn0, 16-byte chunks fn1) |
| 0x04 | 0x1d4b | WirelessDeviceStatus (events only; quiet on query) |
| 0x05 | 0x0020 | ConfigChange: fn0 get-cookie (u16), fn1 set-complete(u16). **Writing it makes the kernel hidpp driver re-enumerate the device** |
| 0x06 | 0x1004 | UnifiedBattery v5: fn0 = capabilities `[levels 0f][flags 0f]`; **fn1 = status** `[soc %][level bits][charging][ext power]` (live `1d 02 00 00` = 29 %, low, discharging). Charging: 0 discharging, 1 charging, 2 slow, 3 complete, 4 error. (Earlier notes read fn0 as status — wrong; matches kernel/Solaar) |
| 0x07 | 0x2250 | XY stats (accumulator stream in fn2; cf. telemetry below) |
| 0x08 | 0x2251 | Wheel stats (fn0 → `[01][00][0x18]`) |
| 0x09 | 0x2202 | **Extended Adjustable DPI** — see §3 |
| 0x0a | 0x8090 | ModeStatus: fn0 → `[status0][status1]`; status1 bits 1..2 = gaming surface (0 auto, 1 on, 2 off → bytes 00/02/04), bit 0 = LightForce switch mode. fn1 set `[status0][status1][mask0][mask1]` — surface write is `00 mode<<1 00 06` (mask keeps the other bits). Decode with the mask: a raw `01` is LightForce, not “on” |
| 0x0b | 0x80E0 | Bunny hopping (scroll filter): fn1 get → window (ms÷10, 0=off, 10..100); fn2 set (⚠ this is a WRITE — querying via fn2 mutates!) |
| 0x0c | 0x1B0C | **ANALOG BUTTONS — the HITS actuation feature** — see §4 |
| 0x0d | 0x8061 | **Extended Adjustable Report Rate** — see §5 |
| 0x0e | 0x8100 | **Onboard Profiles** — see §6 |
| 0x0f | 0x8110 | Mouse Button Spy — present; fn0 → `05`; subscription semantics UNDOCUMENTED, notification grammar is the main open RE target |
| 0x10 | 0x1500 | Force Pairing — ⚠ handle with extreme care (can disrupt pairing); deliberately not automated |
| 0x11–0x23 | 0x1801…0x18B1, 0x9403 | Internal/hidden cluster: 0x1802 device-reset?, 0x1806 cfg-props v8, 0x1805 OOB state, 0x1830 power schemes (unverified), 0x1E00 ADC / “hidden-features unlock?” — future work |

Absent (vs older Logitech gaming mice): 0x2201 classic DPI, 0x8060 classic
rate, 0x807x RGB, 0x1B04 reprog-controls. There is no lighting; button
remapping exists only in the onboard profile.

## 3. Extended Adjustable DPI (0x2202)

- fn0 → sensor count (1).
- fn1 `[sensor]` → axes flags (Y present, LOD present).
- fn2 `[sensor][axis][page]` → paged DPI list: reply params `[sen][axis][page][stream…]`,
  stream = BE16 values, `0x0000` terminates; values with top 3 bits `0b111`
  encode a step range: `(step & 0x1fff)` + next BE16 `end`, expanding from
  last+step upward. Live decode (3 pages, terminator on page 2): 100,
  101–200 /1, 202–500 /2, 505–1000 /5, 1010–2000 /10, 2020–5000 /20,
  5050–10000 /50, 10100–20000 /100, 20125–32000 /125, 32200–44000 /200 →
  **100–44 000 DPI** (matches the spec sheet; an earlier parser stopped after
  page 1 and reported 10 000).
- fn3 `[sensor]` → `[sensor][00][5 × BE16 stage DPI]` (the active profile's
  stages); fn4 → `[sensor][5 × LOD]`.
- fn5 `[sensor]` → `[echo][curX][defX][curY][defY][lodByte]` (BE16 DPIs; live
  800/800 default 800/800, lod byte 2).
- fn6 (LONG) `[sensor][Xhi][Xlo][Yhi][Ylo][lod]` → ACK.
  ⚠ In **onboard mode** the profile stage governs and fn6 has no visible
  effect. openGhub's later G HUB captures show fn6 is what G HUB uses in
  **host mode** (0x8100 mode 2), with LOD 1/2/3; the earlier "no-op" reports
  were taken while profile ownership was changing. Not yet re-measured here.
  In onboard mode use 0x8100 fn12 to pick a stage instead.

LOD encoding in profiles: 0=unused, 1=low, 2=medium, 3=high.

## 4. HITS — Analog Buttons (0x1B0C)

“Haptic Inductive Trigger System”: the main keys are hall-effect-inductive
analog keys with tunable actuation, rapid-trigger, haptics.

- fn0 caps → `[flags=00][buttons=03][actMax=0x28][rtMax=0x14][hapMax=0x14][trailing=01?]`.
  `>>2` gives logical maxima: actuation 10, rapid-trigger 5, haptics 5. Only
  indices 0 (left) and 1 (right) accept calls; index 2 returns nothing.
- fn2 `[btn]` read → `[btn][act<<2][rt<<2|flag][hap<<2]`.
- fn1 (LONG recommended) `[btn][act<<2][(rt<<2)|flag]`… wait, all three values
  ride together: `[btn][act<<2][(rt<<2)|flag][hap<<2]`.
- **Wire encoding: logical<<2 in bits 7..2.** byte2 bit0 is the rapid-trigger
  enable flag: the host may set and clear it (verified live: `08 09 0c`
  accepted and read back, then restored), and G HUB sends it set together
  with the sensitivity (sample below). Solaar treats it as firmware-managed
  and preserves it; its INVALID_ARGUMENT reports (#3202/#3207) came from
  unshifted values hitting the reserved bit 1. Valid quanta: act 0x04..0x28,
  rt 0x04..0x14, hap 0x00..0x14.
- G HUB ground truth wire sample: `11 01 0c 1d 00 0c 09 14`
  (left, actuation 3, rt 2 + flag, haptics 5).
- Writes apply live in host mode. Power-on values are loaded from the profile
  analog block (§6, offsets 0x26..0x2b). G HUB capture swid `0xD` works like
  any other.

Validated live on our unit: read `act=2 rt=2 hap=3 (flag 0)` both buttons —
equal to the profile block `08 08 0c` / `08 08 0c`; round-trip write
2→3→2 verified byte-exact read-back.

## 5. Extended Adjustable Report Rate (0x8061)

- Index order **ascending frequency**: idx 0..6 = 125, 250, 500, 1000, 2000,
  4000, 8000 Hz (labels 8ms…125µs). Bit i in masks refers to idx i.
- fn0 `[conn]` → capability mask u16 BE; conn 0 = WIRED → live `0x000f`
  (125–1000 Hz), conn 1 = LIGHTSPEED → live `0x007f` (all seven, 8 kHz ok).
- fn1 → mask for the link carrying the request (parameter ignored; Solaar
  and openGhub use this one).
- fn2 → current idx (parameter ignored; STALE after writes).
- fn3 `[idx]` → set (single byte; applies to the active link; works live,
  not persistent — persistent value = profile byte §6).

## 6. Onboard Profiles (0x8100) — format 8

- fn0 info → `01 08 01 05 01 05 10 00ff 0a 04` = memory model 1, profile
  format 8, macro format 1, 5 profiles, 1 OOB profile, 5 buttons, 16 sectors,
  **sector size 0x00ff (255)**, mechanical layout 0x0a, various-info 0x04.
- Modes: fn1 set / fn2 get — 0x01 onboard (profile in control), 0x02 host.
- Current profile: fn4 get → sector u16 (live: `0x0001`); fn3 set `[hi][lo]`
  activates (retry ×3 @150 ms — activation times out spuriously).
- Current DPI stage: fn11 get → `[stage]` (live `02`), fn12 `[stage 0..4]`
  set (volatile; this is how G HUB switches stages in onboard mode).
- fn10 → sector CRC list (live `40 37 c5 26 2a 38 2a 38 2a 38 2a 38 …`,
  i.e. sectors 0..5 in order).
- Memory IO: fn5 read `[secHi][secLo][offHi][offLo]` → 16 B chunk;
  fn6 begin `[sec…][len BE16]`; fn7 write 16 B; fn8 commit. Whole-sector
  writes only (flash erase per save), acks unreliable — verify by read-back
  and CRC. **Automated write loops are a flash-wear hazard; never script them.**
- **Reads may not cross the sector end**: offset+16 > 255 answers
  INVALID_ARGUMENT (the "≥240 times out" note was the error-frame parsing
  bug). Read the last chunk at offset 239 to get bytes 239..254. With the
  full sector the CRC verifies: CRC-16/CCITT-FALSE over bytes 0..252, BE at
  253..254 — live sector 1 = `c526`, directory = `4037`, copies of the
  factory profile = `2a38`.
- RAM directory (sector 0): 4-byte entries `[secHi][secLo][enabled][pad]`,
  terminated `ff ff`. Live: 5 slots → sectors 1..5, only sector 1 enabled
  (`00 01 01 ff 00 02 00 ff 00 03 00 ff 00 04 00 ff 00 05 00 ff`). "Enabled"
  = part of the on-mouse profile cycle.
- ROM: 0x0100 = factory directory, 0x0101 = the single factory profile
  (CRC field `ff ff`); 0x0102 answers INVALID_ARGUMENT.

### Profile sector map (format 8, verified on live unit)

| Off | Field (live unit values) |
|-----|--------------------------|
| 0x00 | rate idx wireless (03 = 1 ms) |
| 0x01 | rate idx wired (03) |
| 0x02 | default DPI stage (02) |
| 0x03 | g-shift stage (00) |
| 0x04+5n | stage n: Xlo Xhi Ylo Yhi LOD (LE DPI!) — live: 800/800·med, 1200/1200·med, 800/800·med, 2400/2400·med, 3200/3200·med |
| 0x1d | dpi_delta u32 (0) |
| 0x21 | power_mode (ff) |
| 0x22 | angle snapping (00) |
| 0x23 | write counter u16 (ffff observed — unconfigured) |
| 0x25 | bunny-hop window ms÷10 (00) |
| 0x26 | analog HITS block 2×3 B `[act<<2][rt<<2|flag][hap<<2]` — live `08 08 0c 08 08 0c` = act2/rt2/hap3 both |
| 0x2c / 0x2e | power-save / power-off timeouts (003c / 012c) |
| 0x30..0x6f | buttons 16×4 B `[behavior][type][mask LE]` — factory: `80 01 00 01/02/04/08/10` = send L/R/M/Back/Fwd |
| 0x70..0x9f | g-shift button table (same shape) |
| 0xa0..0xcf | profile name UTF-16LE (live: `PROFILE_NAME_DEFAULT`) |
| 0xd0..0xfb | lighting clusters (`03 … 1f 40` placeholders; RGB-less mouse) |
| 0xfc | lighting flag |
| 253..254 | CRC-16/CCITT-FALSE BE (verified, see above) |

Button entries `[behavior][type/function][b2][b3]`: `0x8X` send — byte 1 is
the type: 01 mouse mask BE16 (1=L, 2=R, 4=M, 8=back, 0x10=fwd), 02
`[modifiers][HID usage]`, 03 consumer BE16; `0x9X` function — **byte 1** is
the function id (3 dpi-up, 4 dpi-down, 5 dpi-cycle, 8 next-profile,
0xA cycle-profile, 0xB g-shift…), byte 3 data; `0x0X` macro; `ff ff ff ff`
default.

openGhub documents a different layout (`[LOD,Xlo,Xhi,Ylo,Yhi]` stages from
offset 3, default stage at byte 1). On this unit that reading gives stage 0
LOD 0, a stray trailing byte, and a default stage of 2400 DPI while the
sensor reports 800 — the layout above (stages from offset 4, default stage
byte 2 = 800 DPI, LOD 2 on all five) is the consistent one.

## 7. Telemetry heard on the wire (undocumented elsewhere)

Two periodic long-frame notifications every ~2 s on the mouse hidraw:

```
[11 01 00 0a 09 00 …]                        root-ish keepalive (feat 0x00, params 09 …)
[11 01 09 5a 00 0320 0320 0320 0320 02 …]    feature 0x09 (DPI) fn5-shaped event:
                                             4×u16 BE (0x0320=800) + trailing 02,
                                             byte-stable while idle
```

A high-rate (~83 Hz) 14-byte `02` stream also flows on the device node:
`02 [hdr][00][s16][s16][ctr][st][slot=01][pid LE bd 40][00][00]` — small ±N
walker; statistics-identical idle vs. moving; believed RF/link-layer noise
floor telemetry. Correlation with 12 ms DeviceInfo interval unexplored.

## 8. Open questions / future work

1. **0x8110 spy notification grammar** — the analog click/force stream. Not
   observed spontaneously; likely requires a subscription write (vendor claim
   via ConfigChange cookie did not surface it within our windows).
2. ~~Profile sector tail (≥240) readability~~ — resolved: aligned last read
   at 239, CRC verified end-to-end.
3. 0x9403 and the 0x18xx/0x1Exx clusters; 0x1830 power schemes.
4. Macro record blob grammar (profile references 12-bit sector + u16 addr).
5. Force-pairing flow for a pure-Linux pairing tool.
6. PPDA/POWERPLAY interaction (0x1000-family absent; charging state byte
   semantics 2=“almost-full/idle”? measured only at 15 %).

## 9. Tool map (this repo)

- `src/ss2k.c` — the driver library (this document implements §§2–6),
  built into `superstrikectl` (CLI) and `libss2k.so` (used by the GUI)
- `gui/superstrike-gui` — GTK4/libadwaita control panel
- `tools/` — python probes used during the RE (probe2, sweep, explore, phases)
- `research/` — session captures (`profile1.bin` = live sector dump), notes
- `research/PROTOCOL-notes.md` addenda land in `docs/`
