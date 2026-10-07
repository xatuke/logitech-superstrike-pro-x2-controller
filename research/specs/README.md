# PRO X2 SUPERSTRIKE — Official Product Intelligence

Research date: 2026-10-07. Topic: official/market knowledge for the Logitech G **PRO X2 SUPERSTRIKE** wireless gaming mouse (USB VID 0x046d, PID 0x40bd, paired via Logitech USB Receiver PID 0xc54d).
Raw captures live in `../../downloads/specs-pages/` (see file inventory at the bottom). All quotes are verbatim from the sources.

---

## 1. Identity, naming, SKU

| Item | Value | Source |
|---|---|---|
| Full product name | **PRO X2 SUPERSTRIKE** ("LIGHTSPEED Wireless Gaming Mouse" subtitle) | logitechg.com PDP variants JSON (`prodpage-enus.html`) |
| Also stylized | "PRO X 2 SUPERSTRIKE" (US PDP SEO title), "PRO X2 SUPERSTRIKE Wireless Analog Gaming Mouse" (HK store) | retailers, logitechclub.com |
| Colorway | **Lunar Eclipse** (two-tone black/white "panda") — the only colorway at launch and apparently globally since | PDP variants JSON, Club386, Micro Center |
| Retail part number / SKU | **910-007700** (appears to be the single worldwide SKU — US, CA, UAE, IN listings all map to it; no 910-007701/2 surfaces in commerce) | PDP variants JSON `productId:"910-007700"`; BestBuy/COLAMCO/CDW/B&H etc. |
| US MSRP | **$179.99** | PDP, TechPowerUp, all retailers |
| Regional prices | CA$239.99 (BestBuy CA), ₹19,495 (Amazon.in ASIN B0GBRGLPMP; second listing B0G3QGG18R), HK$1,399, TRY 9,212 | retailer scrapes via Brave SERP |
| Receiver | Logitech LIGHTSPEED USB receiver, USB ID 046d:c54d (this task's premise); 2.4 GHz; wireless-extender dongle included in box | PDP "in the box"; TechPowerUp review |
| Companion software | Logitech **G HUB**; also supported by standalone **Onboard Memory Manager** utility | PDP; TechPowerUp review p.7 |

## 2. Timeline (2025–2026 product)

- **2025-09-17** — Announced at **Logitech G PLAY 2025** press release: "featuring the world's first haptic inductive trigger system"; preorder on logitechg.com with "retail availability rolling out starting today and continuing through the next two quarters". Source: https://news.logitech.com/press-releases/news-details/2025/Logitech-G-Drops-a-Wide-Array-of-New-Products-and-Innovations-at-Logitech-G-PLAY-2025/default.aspx
- **2025-11 (month of)** — Pro teams win tournaments on prototypes (FURIA IEM Chengdu Nov; G2 Gozen VCT GC EMEA S3 Nov; G2 won VCT Americas Stage 2 on a SUPERSTRIKE prototype) — per launch blog.
- **2025-12-30** — FCC application JNZMR0121 (Wireless Mouse, Logitech Far East, 2402–2480 MHz DTS) — see §7.
- **2026-02-10** — Official launch blog: "PRO X2 SUPERSTRIKE: The Fastest, Fully Customizable Click in Competitive Gaming Lands February 10th" (on-sale date). Source: https://www.logitech.com/blog/2026/02/10/pro-x2-superstrike-the-fastest-fully-customizable-click-in-competitive-gaming-lands-february-10th/
- **2026-02-10 → 2026-02-24** — first wave of reviews (Club386 Feb 10, Insider Gaming Feb 14, ProSettings Feb 24, RTINGS Mar 2).
- **2026-02-20** — Best Buy US listing "Published" stamp.
- **2026-08 (Jul-Sep)** — second review wave incl. TechPowerUp full instrumented review Aug 12 (fw 42.0.11/7.2.11), India launch coverage (FoneArena Aug 10, Croma).

## 3. Official spec sheet (verbatim facets from the logitechg.com PDP JSON, `prodpage-enus.html`)

| Spec (facet) | Value |
|---|---|
| Physical dimensions | 125 mm × 63.5 mm × 40 mm (4.92 in × 2.5 in × 1.57 in) |
| Weight | **61 g** (2.15 oz) |
| Number of buttons | **5** (+ scroll-wheel click ⇒ 6 per TechPowerUp; side buttons LEFT side only — ambidextrous-friendly shell, not symmetric button layout) |
| Sensor | **HERO 2** |
| Resolution (tracking) | **100 – 44,000 DPI** |
| Max speed | **> 888 IPS** (22.6 m/s) — "tested on Logitech G640" |
| Max acceleration | **88 G** — "tested on Logitech G640" |
| Zero smoothing / acceleration / filtering | Yes |
| Max **wired** report rate | **1000 Hz (1 ms)** |
| Max **LIGHTSPEED** report rate | **8000 Hz (0.125 ms)** |
| Battery life | **up to 90 h** ("constant motion", varies with use conditions; HITS draws more current than switches) |
| Charging | USB-C, recharge quickly; **POWERPLAY compatible** (sold separately) |
| Connectivity | LIGHTSPEED wireless via USB receiver; USB-C wire for charging/wired play (rubber cable 1.75 m per TPU) |
| Compatibility | Windows or macOS, USB 2.0+ port |
| Requirements | G HUB needed for advanced features; internet optional for G HUB download |
| Warranty | Two years (TechPowerUp spec box) |
| In the box (retailer+TPU synthesis) | mouse, LIGHTSPEED USB receiver, USB-A→USB-C charging cable, receiver-range extender, grip tape set |

Out-of-box CPI presets (TechPowerUp): **800 / 1200 / 1600 / 2400 / 3200**, five profiles.

## 4. Every G HUB / Onboard-Memory configurable (the "reverse-engineering target list")

From the official PDP feature blocks (verbatim titles) + TechPowerUp's hands-on of the actual software UI (which covers both G HUB and Onboard Memory Manager, since OMM exposes ~the same set):

**Marketing feature blocks on PDP (exact names):**
1. "THE WINNING CLICK" — HITS blurb: "Nothing will slow you down—not microswitches, not latency, not connectivity."
2. "HAPTIC INDUCTIVE TRIGGER SYSTEM (HITS)" — "cutting click latency by up to 30 ms"
3. "TUNABLE ACTUATION & RESET POINTS" — "Choose from **ten actuation points** and **five rapid-trigger reset points**" for both main keys
4. "PRECISE HAPTICS CONTROL" — "**six intensity levels** for a natural, customizable click experience"
5. "DESIGNED WITH PROS" — NAVI, G2, GEN.G, BLG, FlyQuest RED collaborators
6. "PINNACLE OF PERFORMANCE" — HERO 2, 888 IPS, 88 G, 44,000 DPI
7. "ICONIC SHAPE & PROVEN FEEL" — shape carried from PRO WIRELESS / SUPERLIGHT lineage
8. "PLAY AT LIGHTSPEED" — "wired-level speeds with up to 8 kHz polling", "rock-solid reliability—even in RF-heavy esports LANs"
9. "G HUB CUSTOMIZATION & PROFILES" — "Fine-tune your click settings, sensor calibration… **Save profiles to onboard memory**"; community profiles import/export
10. "UNPLUG & LOCK IN" — 90 h battery, USB-C, POWERPLAY

**Actual knob surface measured in software (TechPowerUp p.7 — GOLD for protocol mapping):**
- Tab 1: CPI 100→44,000 in increments (5 color-coded levels; manual numeric entry, truncated to native steps); optional per-axis CPI ("advanced"); **lift-off distance low/medium/high** (requires "Gaming Mode Surface"/"Gaming Surface Mode" enabled in device settings); report rate 125/250/500/**1000 Hz wired** and additionally 2000/4000/**8000 Hz wireless**; sensor surface calibration (copy effective CPI of another mouse).
- Tab 2: button remapping for all buttons (mouse/keyboard/media/**macro**), **shift-button** layer (secondary bind set); **editable only when NOT in onboard-memory mode** (onboard mode allows only enabling pre-set profiles); **device settings always editable** incl. "Gaming Surface Mode" (on/auto/off).
- Tab 3: single option **BHOP mode** — ignores first scroll-wheel detent unless a second detent follows within a time window.
- Tab 4 (HITS): configure **uniformly or per-button (L/R)**:
  - Actuation point: integer **1–10** (1 = shortest travel, "reducing button pre-travel to nearly zero")
  - Haptics level: integer **0–5** (**0 = haptics off**) ← note: OMM/G HUB UI integer range 0-5 vs marketing's "six intensity levels" = same thing
  - Rapid Trigger: integer **1–5** (actual mm per step NOT disclosed anywhere; TPU: "unclear which distance each value corresponds to")
- Profiles: saved live to active profile; profiles persistable to **onboard memory**.
- Battery estimator in G HUB: ≈90 h @1000 Hz, ≈53 h @2000 Hz, ≈39 h @4000 Hz, ≈19 h @8000 Hz (further depends on haptics level).
- Battery gauge: % with "single-digit precision", known inaccurate.
- G HUB quirk (relevant if re-implementing config sync): toggling "advanced" on Sensitivity page shows a *different* set of CPI values than already set.

**Hardware measured by TechPowerUp:**
- Charge current in CC stage: **0.283 A**; battery **290 mAh @ 3.8 V**, 3-pin JST connector (same pack as PRO X SUPERLIGHT 2).
- Wheel encoder: **TTC "blue", 8 mm** (new part); mid-click: unbranded tactile switch.
- Side buttons: **Omron D2LS-21** SMD switches.

## 5. HITS — what it is and how it works (marketing + teardown synthesis)

- Expansion: **"Haptic Inductive Trigger System"** (marketing acronym always "HITS"; tech family name is "SUPERSTRIKE").
- Principle (PDP + launch blog): replaces microswitches with "inductive analog sensor[s] and real-time click haptics"; "custom hardware synced with inductive sensors delivers an immediate tactile response" at actuation/reset crossing.
- TechPowerUp teardown (p.4): "employs **electromagnetic coils, which track button movement by changes in inductance**"; **haptic motors** (plural — main-button PCB) supply tactile feedback; "button stiffness is technically zero" — force curve is purely mechanical mass/spring of the button piece, fully linear, no tactile bump.
- Layout: main-button PCB carries the coils + wheel encoder + wheel switch and mounts to an endoskeleton (same architecture as PRO X SUPERLIGHT 2); connected to main PCB by FFC; MCU rear-mounted on main PCB.
- Controller: **Nordic nRF52833** (datasheet-linked by TPU). Its **Bluetooth radio is unused** by this product — the device speaks Logitech's proprietary 2.4 GHz LIGHTSPEED protocol only.
- Claimed benefit: up to **30 ms click-latency cut** vs microswitches (marketing); measured avg click latency 0.38 ms vs 1.12 ms on orig. SUPERLIGHT (dcprosens blog — third-party oscilloscope).
- Independent criticism (TPU): HITS assembly heavier than switches → +3 g over SUPERLIGHT 2 and front-heavier; **polling instability** at 8000 Hz reported; CPI inconsistencies correlated with surface detection on two pads; excess jitter at high CPI steps.
- PCB production date: **week 47 / 2025**.
- Firmware version current at TPU test time (2026-08): **42.0.11 / 7.2.11** (two-component version string — likely radio/main-app; useful when fingerprinting G HUB OTA payloads).

## 6. Messaging/positioning extras

- PDP push-quote: PC Gamer "THE KIND OF PRODUCT YOU ONLY GET ONCE EVERY FEW YEARS"; Tom's Guide "best wireless gaming mouse you can get right now".
- Esports seeding: Thunderpick World Championship (Oct 2025), IEM Chengdu (Nov 2025) FURIA win, G2 Gozen VCT GC EMEA S3 (Nov 2025), G2 VCT Americas Stage 2 (prototype).
- Pro-player quotes on the launch blog (m0NESY, Kscerato "best click I've ever felt in my life", Siwoo, benjyfishy).
- Follow-ups: successor PRO X3 series teased/reviewed by PC Gamer by Jul 2026 (out of scope).

## 7. FCC filing intelligence (regulatory + teardown leads)

Grantee: **Logitech Far East Ltd, HsinChu, Taiwan — grantee code `JNZ`** (frn 0007134034; contact Rick Lien). fcc.report/fccid.io both Cloudflare-blocked to scripted clients; `fccid.io` pages themselves fetchable via curl. Saved: `downloads/specs-pages/fccid_jnz.html`, `fcc_JNZMR01xx.html`.

Candidate filings around launch (mouse = product code `MR`):

| FCC ID | App date | Radio evidence | Notes |
|---|---|---|---|
| **JNZMR0121** | **2025-12-30** | 2402–2480 MHz, DTS class, **no BLE test report** (matches unused nRF52833 BT radio); freq 2402-2480 | Docs: Users Manual, Internal/External Photos, Test Report, **Theory of Operation + Theory of Operation (G-Sensor)**, Block Diagram, Schematics×2 (metadata only publicly). **Best-guess match for SUPERSTRIKE**: filing window == PCB week-47/2025; BLE-free radio; "G-Sensor" (accelerometer/IMU?) angle fits the surface-detection / auto-sleep logic seen in reports. `LIKELY`, unconfirmed. |
| JNZMR0124 | 2026-01-08 | DTS **GFSK** + **BT LE** test reports | Different mouse profile (would imply BT used). Possibly another product. |
| JNZMR0115 | 2026-01-27 | 2403–2479 MHz; heavy C2PC history; schematics incl. MAINBOARD / FPC / Roller Board | Could be the Superstrike too (roller board FPC fits 2-piece button PCB + separate wheel board), or a re-cert of older SKUs. Unresolved. |
| JNZCU0030 | 2026-05-07 | "Wireless USB Dongle" | Post-launch dongle grant — maybe not the c54d launch receiver; receiver likely covered by an earlier grant/C2PC. |

What the filings promise (public doc set per fccid.io): users manual (statement + rev), internal/external photos, test reports (+ antenna), theory of operation (incl. "G-Sensor"), block diagram, **schematics (metadata only — titles prove a MAINBOARD + FPC + roller-board topology for MR0115)**, label samples. Document PDFs themselves are JS-gated on fccid.io and mirrored on device.report (Cloudflare-blocked in this session) — a TODO with concrete URLs is listed below.

Regulatory-model notation: Logitech internal model for the mouse family is the FCC product code `MR....`; retail-DoC convention is `M-Rxxxxx`. Not yet found for SUPERSTRIKE — Logitech's official compliance search (https://www.logitech.com/en-us/compliance) is an AEM SPA; endpoint needs inspection.

## 8. Open questions / follow-ups

1. **Confirm mouse FCC ID** by reading the printed label (bottom shell: "FCC ID JNZMR..." + model M-Rxxxxx) or the Users Manual PDF of MR0121/MR0115 via device.report mirror (e.g. https://device.report/logitech-far-east/mr0121 guess-path; working doc-hash URLs exist for MR0115: https://device.report/m/66a9697568aba620cd281b7a292afec0b85ea369606bfcf9dd7fe7e79dee2be6_pdf).
2. **Receiver FCC ID for 046d:c54d** — check linux-hardware.org probe DB (usb:046d-c54d) for co-probed device names, and JNZ CU-series grants Dec 2025→Mar 2026.
3. **Numeric actuation/reset distances (mm)** are deliberately NOT published (ten actuation steps 1-10, five reset steps 1-5 integers only). Nailing mm-per-step requires bench calibration (dial indicator) or decompiling G HUB/OMM tooltip assets/firmware strings.
4. HID descriptor + protocol: Onboard Memory Manager supports the mouse over raw HID → capture OMM↔device traffic; compare with PRO X SUPERLIGHT 2 protocol (device reportedly "essentially identical" internally, minus HITS knobs).
5. EU/UK SKU variations & colorway SKUs (if a black-only version ever ships) — watch for 910-0077xx siblings.
6. Mystery "G-Sensor" in FCC theory-of-operation — accelerometer in mouse for surface detection/CPI compensation (matches TPU's observed CPI inconsistencies tied to surface detection)?

## 9. Source captures (raw, untrusted-data dirs)

`downloads/specs-pages/`: `prodpage-enus.html` (logitechg.com US PDP w/ embedded spec JSON + SKU variants), `tpu1/4/5/7/8.html` (TechPowerUp review pages), `fccid_jnz.html`, `fcc_JNZMR0115/0121/0124.html` (fccid.io detail), `lg_compliance.html`, assorted blocked-engine stubs (mojeek/ddg/fcc.report). Remote citations:
- PDP: https://www.logitechg.com/en-us/shop/p/pro-x2-superstrike-mouse
- Launch blog: https://www.logitech.com/blog/2026/02/10/pro-x2-superstrike-the-fastest-fully-customizable-click-in-competitive-gaming-lands-february-10th/
- G PLAY 2025 PR: https://news.logitech.com/press-releases/news-details/2025/Logitech-G-Drops-a-Wide-Array-of-New-Products-and-Innovations-at-Logitech-G-PLAY-2025/default.aspx
- TechPowerUp review: https://www.techpowerup.com/review/logitech-g-pro-x2-superstrike/ (pages 1,4,5,7,8; author pzogel, 2026-08-12)
- RTINGS review: https://www.rtings.com/mouse/reviews/logitech/g-pro-x2-superstrike
- FCC grantee: https://fccid.io/JNZ ; filings: https://fccid.io/JNZMR0121 , /JNZMR0124 , /JNZMR0115
- Retail corroboration for SKU: https://www.bhphotovideo.com/c/product/1949585-REG/logitech_910_007700_pro_x2_superstrike_lightspeed.html , https://www.bestbuy.com/product/logitech-pro-x2-superstrike-lightspeed-lightweight-wireless-gaming-mouse-with-customizable-click-haptics-for-pc-mac-laptop-wireless-white/J7H7ZYL6CY
- Latency measurement (3rd party): https://dcprosens.com/blog/logitech-pro-x2-superstrike-review-guide/
- Brave SERP aggregated retailer list (prices/dates) and review outlets (TechRadar, IGN, Gizmodo, Insider Gaming, Lowyat) as cited in-session.
