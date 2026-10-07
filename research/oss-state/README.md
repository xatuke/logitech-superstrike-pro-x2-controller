# OSS state: Logitech G PRO X 2 SUPERSTRIKE (046d:40bd, receiver 046d:c54d)

Research date: 2026-10-07. Investigator: oss-state subagent.
Local artifacts (untrusted-downloaded content, inspect only):
- `downloads/solaar-clone/` — git clone of github.com/pwr/Solaar, HEAD `e7304c4c451cc9bb4f206a914844525e67856a28` (2026-08-18)
- `downloads/libratbag-clone/` — git clone of github.com/libratbag/libratbag, HEAD `8235b5bb6032dea15901ab58a8218956f4494d08` (2026-09-17)
- `downloads/libratbag-pr1896/pr1896.patch` — full 11-commit patch series of libratbag PR #1896
- `/tmp/hidpp_master.c` — torvalds/linux master `drivers/hid/hid-logitech-hidpp.c` (5023 lines), fetched 2026-10-07

Confidence labels: VERIFIED = I read the code/issue/API response myself this session. LIKELY = strong indirect evidence. UNCERTAIN = flagged as such.

---

## TL;DR

Existing support is concentrated in **userspace Solaar (mature, shipped)** and an **unmerged libratbag PR (complete but unreleased)**. The Linux kernel has **zero Superstrike-specific code**; the c54d receiver and the mouse ride the generic `hid-logitech-dj` + `hid-logitech-hidpp` machinery. No third-party RE write-up/blog/gist of the Superstrike protocol was found. **The actuation/rapid-trigger/haptics protocol (HID++ feature 0x1B0C "HITS", ANALOG_BUTTONS) is already fully decoded in Solaar** — do not re-derive it from scratch; there is essentially no other public source.

---

## 1. Linux kernel

### 1.1 torvalds master (VERIFIED, fetched 2026-10-07)
- `drivers/hid/hid-logitech-hidpp.c` master = 5023 lines; `grep -i "40bd|c54d|superstrike|lightstrike|analog_button|1b0c"` → **zero matches**.
  https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-logitech-hidpp.c
- Consequence: the kernel has no PID whitelist entry or quirk for the mouse; it works through the generic HID++ path (hidpp auto-enumerates feature list from the device). Nothing kernel-side to port/implement for tuning — that belongs to hidraw userspace.
- Recent commit history for that file (GitHub commits API, VERIFIED): newest = `2026-09-23 Revert "HID: logitech: add Bolt receiver support for Logitech HID++ devices"`; the Bolt receiver support it reverts was added 2026-08-10. Other 2026 entries: G502 X Lightspeed USB (2026-09-01), FF init refactor, reprogrammable-buttons work (2026-07-04), G502 X Plus (2026-07-21). **Nothing Superstrike/HITS/0x1B0C-related in 2025–2026.**
- `drivers/hid/hid-ids.h` master: `0xc54d = USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1_4`, `0xc545 = ..._LIGHTSPEED_1_5`, `0xc548 = ..._BOLT_RECEIVER` (VERIFIED).
- `drivers/hid/hid-logitech-dj.c` master line ~2106: `{ /* Logitech lightspeed receiver (0xc54d) */ ... USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1_4 }` — the receiver is bound by **hid-logitech-dj.ko** (VERIFIED). Paird 40BD shows up as an DJ-device child with its own hidraw; hid-logitech-hidpp claims DJ devices via the LOGITECH_DJ device group. Practical note: interacting with the mouse = open its hidraw node and speak HID++ short/long reports.

### 1.2 Mailing list (linux-input / LKML) via marc.info (VERIFIED empty result)
- Body+subject search across ALL marc lists and within `linux-input`: `superstrike` → 0 threads; `40bd` → 0 threads; `"pro x 2"` → 0 threads. (Same query method validated positive with `logitech`.) https://marc.info/?w=2&r=1&s=superstrike&q=b
- Caveat (UNCERTAIN): marc numeric-token indexing is imperfect; but the code-side absence corroborates. lore.kernel.org itself sits behind Anubis anti-bot and could not be queried directly this session.
- Adjacent signal (VERIFIED, unrelated): ongoing 2026 thread "[PATCH v9] HID: logitech-hidpp: Add support for HID++ Multi-Platform feature (0x4531)" by an outside contributor — newest *discussion* around adding a new HID++ feature handler to the kernel driver, and none involve 0x1B0C. https://marc.info/?l=linux-input&m=179077538602386&w=2

## 2. Solaar (github.com/pwr/Solaar) — the crown jewel

Shipped, mature support. Head commit inspected 2026-08-18.

### 2.1 Device identity + full feature dump (VERIFIED: `docs/devices/PRO X 2 Superstrike 40BD.txt`)
Captured against "Solaar version 1.1.19"-era runtime:
- WPID **40BD**, codename PRO X 2 Superstrike, kind mouse, **Protocol HID++ 4.2**, report rate 1ms, Model ID `40BDC0A80000` (transport usbid `C0A8`), bootloader BL2 73.00.B0011, firmware MPM 42.00.B0011.
- 36 enumerated HID++ 2.0 features, of interest:
  - `3: DEVICE NAME {0005} V5` → device reports name "**PRO X2 SUPERSTRIKE**"
  - `9: EXTENDED ADJUSTABLE DPI {2202} V0`
  - `10: MODE STATUS {8090} V3`
  - `11: unknown:80E0` (later named `BUNNY_HOPPING` in Solaar's constant table; device doc conjectures it is a non-functional haptics stub)
  - `12: SUPERSTRIKE TUNING {1B0C} V0` ← **the adjustable-actuation feature (HITS)**
  - `13: EXTENDED ADJUSTABLE REPORT RATE {8061} V0`
  - `14: ONBOARD PROFILES {8100} V0` (modes: Host "On-Board-Disabled" vs Profile 1)
  - `15: MOUSE BUTTON SPY {8110} V0`
  - `16: FORCE PAIRING {1500} V0`
  - hidden/internal ones incl. `unknown:9403` (doc's hypothesis: inaccessible "BHOP" bunny-hop feature), cluster of `1E00/1E02/1E22/1E30/1EB0/18xx`.

### 2.2 HITS / 0x1B0C protocol — fully decoded (VERIFIED in same doc + `lib/logitech_receiver/settings_templates.py`)
- Name expansion: **HITS = Hall-Effect Inductive Trigger Switch** (per PR #3132 text).
- Function 0x00 GetCapabilities → bytes `[flags, button_count, max_actuation(wire), max_rapid_trigger(wire), max_haptics(wire)]`; firmware reports button_count 3 but **only indices 0 (left) and 1 (right) are user-settable**; observed maxima 40/20/20 (wire units).
- Read (fn **0x20**, param: button_index) → `[index, actuation, rapid_trigger, haptics]`.
- Write (fn **0x10**) → `[button_index, actuation, rapid_trigger, haptics]`.
- **Wire encoding (critical):** each of the three values packs the *logical* value in bits 7..2 → `wire = logical << 2`; i.e. actuation 4–40 in steps of 4, haptics ∈ {0,4,8,12,16,20}, rapid trigger 1–20. Additionally **byte 2 bit 0 is a firmware-managed `sensitivityFlag` that MUST be carried over unchanged** from the read when writing, otherwise the device returns `INVALID_ARGUMENT` (see issue #3202). Documented in the code comment block above `_AnalogButtonActuationRW` (settings_templates.py ~line 4188).
- Solaar setting objects: `analog-button-tuning_actuation-{0,1}`, `analog-button-tuning_rapid-trigger-{0,1}`, `analog-button-tuning_haptics-{0,1}` (logical ranges 1–10, 1–5, 0–5). Older 1.1.19 capture/doc used names `superstrike-tuning_*` and raw wire values; a value-migration shim `_AnalogButtonSetting._pre_read` handles persisted legacy raw bytes.
- Constants: `lib/logitech_receiver/hidpp20_constants.py` — `ANALOG_BUTTONS = 0x1B0C`, `EXTENDED_ADJUSTABLE_DPI = 0x2202`, `XY_STATS = 0x2250`, `WHEEL_STATS = 0x2251`, `EXTENDED_ADJUSTABLE_REPORT_RATE = 0x8061`, `MODE_STATUS = 0x8090`, `BUNNY_HOPPING = 0x80E0`, `MOUSE_BUTTON_SPY = 0x8110`.

### 2.3 History (VERIFIED via GitHub API)
- **PR #3132** "feat: Add PRO X 2 Superstrike mouse support with HITS tuning settings" — author **caioquirino**, created 2026-02-11, **merged 2026-04-12**. Entered the changelog under release **1.1.20** (CHANGELOG.md line 54). https://github.com/pwr/Solaar/pull/3132
- **Issue #3202** (2026-04-29, closed): "Analog Button Actuation Point and Rapid Trigger fail for values below 4" on PRO X2 SUPERSTRIKE (Arch, kernel 6.19.x, solaar 1.1.19-46-gff9324d3) — root-caused to the wire/logical encoding described above; fix = the `<<2` + sensitivityFlag-preserve logic now in the code. https://github.com/pwr/Solaar/issues/3202
- Release lineage check (VERIFIED): settings_templates.py in tag **1.1.20** vs master is **byte-identical (matching md5 220ac4fe...)** → the fixed analog-button implementation is in the released 1.1.20, not only master.

### 2.4 Receiver (VERIFIED: `lib/logitech_receiver/base_usb.py`)
- `LIGHTSPEED_RECEIVER_C54D = _lightspeed_receiver(0xC54D)` present in `KNOWN_RECEIVERS` (as are c545/c547). Receiver metadata (name "Lightspeed Receiver", etc.) comes from this table; pairing over c54d is supported by Solaar (`FORCE PAIRING {1500}` also present on the mouse).

### 2.5 Other Solaar docs worth mining for the wider effort
- `docs/PRO_X2_SUPERSTRIKE_CLI.md` — full CLI walkthrough for the mouse: `solaar config <dev> <setting> [value]`; notes that many settings need `onboard_profiles Disabled` first; `report_rate_extended` accepts `8ms,4ms,2ms,1ms,500us,250us,125us` (i.e. advertised support down to **8000 Hz**).
- `docs/features.md`, `docs/capabilities.md` mention Superstrike/X2 context (general framework docs).

## 3. libratbag / piper

### 3.1 master state (VERIFIED, HEAD 8235b5b 2026-09-17)
- `grep -ri "40bd|superstrike"` over the whole repo → **zero matches**. `grep "0x2202|0x8061"` in `src/hidpp20.{c,h}` → zero. Only `data/devices/logitech-g-pro-x-wireless-superlight.device` (Superlight **1**) exists. Master libratbag cannot drive the Superstrike (nor Superlight 2).

### 3.2 PR #1896 (VERIFIED patch downloaded: `downloads/libratbag-pr1896/pr1896.patch`)
- "hidpp20: add support for G Pro X Superlight 2 and PRO X 2 Superstrike", author **Nathan Rossi** (series starts 2025-01-12), opened 2026-09-10, **state open / unmerged**, rebased on master. https://github.com/libratbag/libratbag/pull/1896 — closes issues **#1572** (darix, closed) and **#1519** (open).
- Implements:
  - **0x2202 Extended Adjustable DPI** — opcode map (VERIFIED from patch): `GET_SENSOR_COUNT 0x00`, `GET_SENSOR_DPI_AXES 0x10`, `GET_SENSOR_DPI_LIST 0x20`, `GET_SENSOR_DPI 0x50`, `SET_SENSOR_DPI 0x60`; per-axis X/Y DPI + lift-off distance preserved on write.
  - **0x8061 Extended Adjustable Report Rate** — `GET_REPORT_RATE_LIST 0x00` (returns a non-linear bitmap of supported rates), `GET_REPORT_RATE 0x20`, `SET_REPORT_RATE 0x30`; connection types enum `WIRED = 0x00`, `LIGHTSPEED = 0x01` (separate report rate per transport!).
  - **Onboard profile formats 0x06 (GPXSL2) and 0x07 (GPX2_BHOP)** with UTF-16 profile names. Format 0x07 reuses a reserved field of 0x06 — comment ties it to the "bhop" (bunny-hop) variant, matching the hidden 0x9403/0x80E0 BHOP hints in Solaar.
  - Device files: `usb:046d:40bd;usb:046d:c0a8` (Superstrike), `usb:046d:40a9;c09b` (Superlight 2), `usb:046d:c09a/c543` (G Pro 2 Lightspeed).
  - Probe robustness note (worth knowing): GPX2 firmware can reject the user profile directory; switching to onboard mode too early can leave **empty button maps**; PR falls back to ROM/host mode until a valid directory read succeeds.
- Issues context (VERIFIED via API): #1519 (2023-09-19) user request "G Pro X Superlight 2 unsupported"; #1572 (2023-12-22) darix began support work, includes USB device descriptors showing `046d:c54d USB Receiver` + `046d:c09b PRO X 2` direct-wired enumeration, and mentions he sent a kernel-side USB ID patch upstream.

### 3.3 Takeaway
Until #1896 merges and a release cuts, **piper cannot tune this mouse at all**; Solaar is the only functional GUI/CLI path today.

## 4. Arch Linux packaging (VERIFIED, archlinux.org package JSON API, 2026-10-07)

| pkg | repo | version | last update | implication |
|---|---|---|---|---|
| solaar | extra | 1.1.20-2 | 2026-07-19 | **includes full Superstrike HITS support + #3202 encoding fix** (tag md5-checked) |
| libratbag | extra | 0.18-1 | 2024-09-24 | ~2 years stale; predates PR #1896 entirely → no Superstrike/Superlight 2 |
| piper | extra | 0.8-5 | 2026-07-19 | front-end only; inherits libratbag's lack of support |
| hid-tools | extra | 0.12-2 | 2026-01-10 | generic HID capture/record tool available for live tracing |

Practical: on this machine (Arch, kernel 7.1.8) `solaar` can already set actuation/rapid-trigger/haptics/DPI/report-rate; for raw experimentation use `hid-tools`/`hidrd` or direct hidraw writes.

## 5. Community RE efforts / write-ups

Found **nothing** beyond Solaar and libratbag PR #1896:
- Hacker News (Algolia API, VERIFIED): no relevant hits for "superstrike" (all years).
- Google News RSS (VERIFIED): 0 items for "superstrike (logitech OR linux OR firmware)".
- Reddit JSON API: blocked from this network (HTTP-level refusal); DDG lite captcha-walled; Mojeek/Startpage/Bing via curl either consent-walled or JS-shell → no result text extractable. WebSearch tool broken this session (API 400). GitHub code search requires auth (401) so gist-hunting was not exhaustive.
- UNCERTAIN/hearsay zone: nothing credible surfaced to repeat.

Related precedents useful as references for implementation style (VERIFIED existence via Solaar code):
- Generic 0x8061 "extended report rate" support in Solaar master (choice list with µs units), reusable regardless of device.
- `MODE STATUS 0x8090 V3` — Superstrike-specific mode toggles not decoded anywhere yet (Solaar displays it as status only).
- Hidden feature cluster `0x1803, 0x1817, 0x1830, 0x1877, 0x1861, 0x1890, 0x18A1, 0x1E00/02/22/30/B0, 0x18B1, 0x1602, 0x9403` remain undocumented — open surface for original RE work.

## Recommended next steps (from these findings)

1. Base the Linux driver/tooling on Solaar's decoded 0x1B0C semantics (`downloads/solaar-clone/lib/logitech_receiver/settings_templates.py`, functions `_AnalogButton*RW`, and `docs/devices/PRO X 2 Superstrike 40BD.txt`) instead of black-box probing the tunables; spend probe effort instead on the *undecoded* features (0x8090 MODE STATUS, 0x80E0/0x9403 BHOP, 0x8110 MOUSE BUTTON SPY semantics, 0x1E0x hidden set).
2. Track/comment on libratbag PR #1896 (Nathan Rossi) — merging it eventually brings piper support; its 0x2202/0x8061 opcode tables + format 0x07 profile layout are the best published documentation of those features.
3. Use the device doc's caps negotiation (fn 0x00 → maxima) to make implementation device-adaptive rather than hardcoding 40/20/20.
4. If kernel exposure is desired later (evdev events for pressure/actuation), no precedent exists upstream — would be net-new drivers/hid work; the community precedent for HID++-derived evdev extensions lives in hid-logitech-hidpp.c reprog/highres code.
5. Ping paths likely productive for further intel that I could not exhaustively cover: Solaar discussions for PR #3132 comment thread (protocol hands-on), and any G Hub firmware dumps (covered by a sibling agent presumably under research/ghub/).
