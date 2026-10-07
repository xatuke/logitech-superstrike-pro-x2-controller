# Research log — protocol reference topic (negative results + verification)

Date: 2026-10-07. Companion to `features.md`.

## Library hunt for "LibreHidDocs"

Brief指定的 `git clone https://github.com/cvut/librehiddocs` → `fatal: repository 'https://github.com/cvut/librehiddocs/' not found`.

Verification trail:
- Bing `"LibreHidDocs" github` → only Puzzlewood tourism noise (irrelevant SEO).
- Mojeek search → HTTP 403 to fetcher.
- GitHub repo search UI (`/search?q=librehiddocs&type=repositories`) → 0 results.
- GitHub API `search/repositories?q=librehiddocs` → `total_count: 0`.
- GitHub API org scan of `cvut` (2183308, real org) → no hid/logi-named repos.
- Slug probes returning 404: `pwr-Solaar/librehiddocs`, `pwr/librehiddocs`, `cvut/LibreHidDocs`,
  `MatMoul/lighthouse`, `MatMoul/g-hid`, `FFY00/hidpp`.
- Anonymous GitHub code search → requires auth (401), not pursued.

Substitute lineage used (endorsed by Solaar itself in
`lib/logitech_receiver/hidpp20_constants.py:22-24`): names sourced from
`github.com/cvuchener/hidpp` + `github.com/Logitech/cpg-docs` hidpp20 tree; the current
living protocol docs are Solaar's own `docs/` markdown; the git-clonable Solaar wiki
(pwr-Solaar/Solaar.wiki.git, HEAD 9b908ca899dbca17afc11c1523aa7f6bee329db5) is sparse.

## Discovery that changed the task

`grep -ri superstrike` over the Solaar clone immediately showed the Aug-2026 Solaar carries
native PRO X 2 Superstrike support (changelog entry #3132, docs, device dump, CLI reference).
Consequence: features are device-confirmed rather than inferred; PID 40BD, HID++ 4.2,
feature `0x1B0C` all appear in-tree.

## Ecosystem survey notes

- `MatMoul`: author is g810-led; **no** lighthouse repo (verified via /users/MatMoul/repos).
- "lighthouse" family in brief: closest maintained OSS touching gaming-tier Logitech mice are
  Solaar (settings-capable), libratbag/ratbagd (profile-capable), logiops (PixlOne; enum
  predates 0x2202/0x8061 — `backend/hidpp20/feature_defs.h`), kernel hidpp.
- "LIGHTFORCE" by name: zero hits in any cloned tree; the analog-actuation surface on mice is
  `ANALOG_BUTTONS 0x1B0C` (+ FORCE_SENSING_BUTTON 0x19C0 on other hardware).
- OpenRGB logitech plugin: content-list probing inconclusive anonymously; excluded from the
  compiled table rather than risking unsourced layouts.
- WebSearch tool was erroring (schema/API 400) throughout → all web access via WebFetch
  (bing/mojeek/github api/raw) and `curl` to api.github.com.

## Clone manifests

- `downloads/refs/Solaar` @ e7304c4c451cc9bb4f206a914844525e67856a28 (2026-08-18, "Align the colons...")
- `downloads/refs/libratbag` @ 8235b5bb6032dea15901ab58a8218956f4494d08 (2026-09-17, "add ASUS TUF Gaming M3 Gen II")
- `downloads/refs/logiops` @ e15799553f97c1b8bab5d9b22b58453513b56217
- `downloads/refs/Solaar.wiki` @ 9b908ca899dbca17afc11c1523aa7f6bee329db5
- `downloads/refs/kernel/hid-logitech-hidpp.c` — torvalds/linux master raw fetch, 5023 lines

No downloads executed or run; all reads/static inspection.
