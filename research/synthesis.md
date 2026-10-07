# Completeness critique — PRO X2 SUPERSTRIKE (046d:40bd) Linux RE

Critique of five researcher reports (R0 specs, R1 OSS state, R2 G HUB, R3 protocol, R4 community),
cross-checked against live-hardware captures in `research/live-sweep.txt` (plus `phases.cap`,
`session.cap`), which the researcher JSONs themselves do not cite.

Bottom line: the driver-blocking core is already solved and cross-validated by four independent
codebases plus our own live probe (HID++ 4.2, dev index 0x01 over receiver c54d / wired c0a8,
runtime feature resolution, 0x1B0C HITS tuning RMW with `logical<<2` packing and the rapid-trigger
byte's firmware bit0, 0x2202 / 0x8061 / 0x1004 / 0x8100 skeletons). Remaining risk is concentrated
in persistence and layout semantics, not in talking to the device.

## 1. Contradictions

- C1. Meaning of 0x80E0 and locus of BHOP. R3: "0x80E0 is a non-functional stub", BHOP suspected at 0x9403. R4: 0x80E0 is the functional bunny-hopping scroll window (fn1 get / fn2 set, wire = ms/10, 100–1000 ms), 0x9403 unidentified, correcting Solaar. Supporting R4: profile byte 0x25 stores the same ms/10 quantity. Our live sweep: 0x80E0 funcs time out on empty params (reachable, not INVALID_FEATURE) — undecided. OPEN.
- C2. Live DPI setter 0x2202 fn6. R3 instructs implementing it (host mode); R4 measured it as a firmware no-op (movement ratio 1.0) with a stale getter, DPI persisting only via the onboard profile sector. OPEN; re-test on our unit under both modes.
- C3. LOD encoding. R3 (0x2202 fn5 reply offset 9): 0=LOW/1=MED/2=HIGH. R4 (profile stage byte): 1=Low/2=Med/3=High, 0 = "stage unused". Live fn5 tail byte reads 02 — ambiguous under either scheme. OPEN.
- C4. Onboard-profile record layout. R3/Solaar-generic: buttons @0x20, 5 DPIs u16LE @0x03, RGB @13, power @16, LED effects @208. R4 format 8: five 5-byte stages [Xlo,Xhi,Ylo,Yhi,LOD] @0x04, buttons @0x30, analog block @0x26, name @0xA0, CRC-16/CCITT-FALSE @253–254, 255-byte sectors. Only name@0xA0 and CRC-presence agree; everything else collides — almost certainly a format-6-vs-format-8 mix-up (libratbag is blocked at "layout 0x06"). OPEN; one empirical sector dump settles it.
- C5. DPI ceiling. 44,000 (R0, official spec) vs "up to 32000" (R3, feature-note claim). Neither has the actual 0x2202 fn2 list; our sweep shows only the first page (100, 200?, 500, 800, 1200, 2400, 3200, ...). OPEN.
- C6. Initial rapid-trigger default. R3 validates with rt=3 (wire 0x0C); R4's factory ROM analog block `14 08 0c` gives rt=2; live read `00 08 08 0c` (rt=2, flag bit0=0). Probably firmware-vs-profile divergence plus the flag bit; needs a flag-aware recount. Minor.
- C7. Acronym HITS: "Haptic Inductive Trigger System" (R0, marketing) vs "Hall-Effect Inductive Trigger Switch" (R1). Cosmetic; pick marketing's for docs.
- C8. Wired report-rate ceiling. R0: official wired max 1000 Hz. R3: unqualified caps "index set [0..6], up to 8000 Hz". SETTLED BY LIVE CAPTURE: 0x8061 fn0 takes a conn-type param; wired(0) mask = 0x0f (125–1000 Hz), LIGHTSPEED(1) mask = 0x7f (through 8000 Hz); fn2 get also needs the param (INVALID_VALUE without). R1/R3 must be amended accordingly.
- C9. Firmware naming: "42.0.11 / 7.2.11" (R0, TPU teardown) vs "MPM 42.00.B0011 / bootloader BL2 73.00.B0011" (R3). Live DeviceInfo component strings "MPMB" / "BL2s" back the two-component naming; adopt R3's form and note the TPU shorthand. Minor.
- C10. 0x1B0C caps arity: 5-element summary (R1, R3) vs 6 bytes with trailing 0x01 (R4). SETTLED BY CAPTURE: `00 03 28 14 14 01`; the trailing 0x01 is real and its meaning is unknown (present-flag?).
- C11. UNIFIED_BATTERY function assignment. R3: fn0 = caps, fn1(status). Live sweep: fn0 returns a 4-byte status-shaped reply `0f 0f 02 00`, fn1 a 2-byte caps-shaped `1d 02` — inverted relative to R3 (and matching the kernel's func0=status/func1=caps). Verify against kernel cmd IDs before trusting R3's line refs. Minor.

Non-contradictions worth stating once: device index 0x01 over c54d / multi-index on wired c0a8 (R4) is consistent with R1/R3; zero kernel-side Superstrike code with c54d already bound by hid-logitech-dj is agreed by R1 and R4; getters-stale (R4) subsumes R3's clean read paths.

## 2. Gaps — what a driver implementer still cannot answer

- Byte-exact, CRC-verified format-8 sector map for THIS unit (C4), sector count/roles (live 0x8100 info `01 08 01 05 01 05 10 00 ff 0a 04` suggests 5 profiles, 0x00FF sector size, 0x0A sectors), RAM-directory vs ROM sector handling, and the pristine-unit FFFF-CRC bootstrap path (currently single-sourced, mclol0 issue #1).
- Persistence matrix: for each knob (DPI, LOD, rate, HITS trio, buttons/macros, surface mode, BHOP) — live-only vs profile-backed; which require onboard=host mode; the "force onboard before valid directory read blanks button maps" trap formalized.
- 0x8090 MODE STATUS: R4's layout is "likely" only (Auto=00/On=02/Off=04, set fn1 `00 mode 00 06`); what "LightForce" is; sensor indexing; whether it gates LOD/auto-surface.
- BHOP end-to-end: enable flag vs window value, live feature (C1), relation to profile byte 0x25 and wheel behavior.
- 0x8110 MOUSE BUTTON SPY: notification format completely undocumented — this is the device's marquee analog data stream (per-button travel), potentially the most interesting Linux-side feature.
- Macro storage: behavior-nibble 0/1 references a 12-bit sector + u16 address, but the macro blob grammar (length, event encoding) is unknown anywhere.
- 0x1B0C: purpose of button index 2 (caps say 3, profile block has 2); how G HUB "uniform" rapid-trigger maps to indices; life cycle of the firmware-managed bit0 sensitivity flag.
- G-sensor / accelerometer channel (FCC "Theory of Operation"): no one has chased whether it is exposed (0x2250/0x2251/0x8090/hidden pages); it is the best hypothesis for reviewer-reported CPI anomalies.
- Hidden-feature access: 0x1E00 "enable hidden features" is itself hidden; no demonstrated unlock for 0x9403 / 0x18xx / 0x1Exx.
- Physical mm per actuation/rapid-trigger step (UX nicety only).
- G HUB model_id and per-device dataset for 0x40BD (R2: needs a Windows VM capture) — parity/reference value only now.
- 8 kHz link stability over LIGHTSPEED: reviewer instability reports vs R4's single clean 1000 Hz measurement; no jitter data at 2000/4000/8000.
- Receiver-side FCC ID for c54d (R0, uncertain) — minor.

## 3. Top 10 action items (by value)

1. Full read-only dump of all 0x8100 sectors from a G-HUB-configured unit; diff against R4's format-8 map and R3's table; build the CRC-16/CCITT-FALSE verifier and a dump-before-write tool. Resolves C4 and makes every persistence change safe.
2. Single scripted Windows G HUB session (USBPcap) toggling surface Auto/On/Off, BHOP on/off + window, uniform vs per-button rapid trigger, and recording one macro. Densest unknown-resolution per hour: settles C1, the 0x8090 layout, uniform-index mapping, and macro grammar at once.
3. Persistence-matrix bench: set each knob live, verify by measurement (report counting / movement ratio), power-cycle, re-check; repeat with onboard On vs Disabled. Settles C2 and the host-mode gating trap in one experiment series.
4. LOD bench: drive every encodable value through both the 0x2202 byte and the profile stage byte, measure real lift-off height. Fixes C3 and validates per-stage LOD bytes.
5. Decode 0x8110 MOUSE BUTTON SPY: listen for notifications while pressing/releasing/sliding on buttons with the sensor idle; determine whether analog travel is streamed. Unique device capability for Linux.
6. 0x1B0C edge work: confirm caps incl. trailing 0x01, probe button index 2, log rt-byte bit0 across mode/surface changes, reconcile factory-vs-live defaults (C6). Cheap, protects every future write.
7. Finish the rate layer: conn-type-parametrized fn0 masks (already captured: wired 0x0f, LIGHTSPEED 0x7f), fn2 get with param, fn3 set per link, jitter measurement at 2000/4000/8000 Hz, and profile bytes 0/1 persistence check. Extends live-sweep.txt.
8. Full paginated dump of the 0x2202 fn2 DPI list to its terminator; publish the exact table. Settles C5 (44k vs 32k) and decodes fn1 caps `00 05 0f`.
9. Assemble the Linux driver core from the four oracles (linux-superstick framing, openghub profile/CRC, kazehana 1B0C, Solaar RMW): hidraw discovery (HID_ID 046d filter, ping 0x01 with 350 ms timeout, c0a8 multi-index disambiguation), runtime feature-index resolution, bit-preserving RMW, ACK-less write timeouts (~100 ms) with retries, CONFIG_CHANGE 0x0020 SetComplete after pushes, verify-by-measurement policy. No kernel changes needed.
10. G-sensor and hidden-cluster probe: reversible ROOT by-ID enumeration and cautious 0x1E00 unlock attempts for 0x9403/0x18xx; correlate 0x2250/0x2251/0x8090 during surface transitions to explain the CPI-anomaly reports and possibly expose an accelerometer stream.
