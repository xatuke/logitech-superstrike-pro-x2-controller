# G HUB static analysis (for X2 SUPERSTRIKE / PID 0x40BD support)

Topic deliverable: obtain the official G HUB installer, statically dissect it, determine
where per-device settings data lives, what protocol hints are visible in plaintext, and
whether the PID 0x40BD dataset can be fetched directly. Date: 2026-10-07.

## 1. Official installer (obtained, verified)

| Item | Value |
|---|---|
| Source page | https://www.logitechg.com/en-us/innovation/g-hub.html (official "DOWNLOAD G HUB" button) |
| Windows URL | `https://download01.logi.com/web/ftp/pub/techsupport/gaming/lghub_installer.exe` |
| HTTP | 200, `application/octet-stream`, S3/CloudFront, `last-modified: 2026-08-18` |
| Size | 70,346,392 bytes |
| SHA-256 | `4b2f9903b27c8434afcd52fe65845632fcae47cc50432fb6b3b1637144e811e1` |
| ETag | `d6a52775b3c74961a42eb0252f48c121` |
| File version | 2026.4.9028.0 (product 2026.4.919028), Linker 14.38, built 2026-06-26 |
| Identity | PE32+ GUI; `InternalName: lghub_installer.exe`, Company "Logitech, Inc."; Authenticode cert present (DigiCert-chain timestamping 2025 CA seen in strings) |

macOS counterpart (linked from the same page): `https://download01.logi.com/web/ftp/pub/techsupport/gaming/lghub_installer.zip`
— HTTP 200, 8,436,943 bytes, etag `bc0252c609a722fa9aed1239b7e8b59d`, also a thin bootstrapper (not downloaded).

Local copies live under `downloads/ghub-installer/`
(`lghub_installer.exe` plus carved artifacts — all treated as untrusted data, never executed).

## 2. Container identification

- `7z l` identifies Type = **PE**; resources tiny; the giant `.text` (69,233,698 bytes) is the
  payload area. 7z extraction yields: `.text`, `CERTIFICATE` (10,392 B Authenticode), `.rsrc`
  (icons, MANIFEST, version.txt = "Logitech G HUB Setup").
- Strings show a **WiX Toolset Burn bootstrapper** engine (`WiX Toolset Bootstrapper`,
  burn engine logs "Detect begin, %u packages", `c:\agent\_work\36\s\wix\src\burn\engine\package.cpp`)
  combined with a **.NET self-contained WPF custom BA** (ILRepacked `lghub_setup`,
  pdb path `C:\builds\kragle\lego\...\frontend_zero\lghub_installer\windows\setup\Release\ILRepack-...`),
  native downloader (`logi::pipeline::*`, `logi::downloader::*` with WinHTTP and libcurl transports),
  Sentry-native crash reporting, WebSocket++, boost asio, protobuf.
- binwalk inside `.text`: 3 embedded x86/x64 PE binaries + **6 Microsoft CAB archives**.
- `innoextract` is NOT installed (`pacman -Q innoextract` errors); not needed — the container is
  not Inno Setup. Formats that worked: **7z (PE recursion) + bsdtar/unzip for CABs**.

### Embedded package inventory (all carved + classified)

| Offset in `.text` | Object | Content |
|---|---|---|
| 7,380,325 | x64 DLL (7 sections) | native installer/shared glue (`logi::installer::*`, pipeline client, crash paths) |
| 16,641,033 | CAB 195,729 B (33 files) | `0` (XML) + `u0..u31`: .NET bootstrapper UI assets (RTF EULA fragments, XML, PNG) + i386 DLL |
| 16,847,161 | CAB 13,253,349 B (12 files `a0..a11`) | **MSI (a0, .NET Foundation, 212,992 B)** + Windows Standalone-Update MSU CABs (KB patches, 2015-2018 dates) + nested 11 MB CAB (28 files) |
| 30,797,061 | CAB 24,945,422 B (14 files `a0..a13`) | same pattern: 3 x 212,992 B .NET-Foundation MSIs + MSU patch CABs + nested runtime CABs |
| 56,236,625 | CAB 193,619 B (33 files) | repeat of the u-series |
| 56,436,649 | CAB 11,028,691 B (2 files) | .NET runtime MSI + nested 11 MB CAB |

Verdict: **everything embedded = prerequisites only** (Microsoft .NET Desktop Runtime MSIs +
MSU patches + bootstrapper UI). No Logitech G HUB application code, no device data, no datasets.

## 3. Per-device data is NOT in the installer — the runtime distribution system

Strings of the native payload expose the whole delivery architecture:

- Virtual package URIs: `pipeline://core/LGHUB/lghub_software_manager.exe`
  (`logi::installer::depot_extensions::extract_pipeline_uri` maps them to real URLs).
- Base hosts (defaults, switchable by feature flags `apps_updater_use_env_staging/_dev`,
  `scarif_dev_enabled`):
  - `https://pipeline.logitech.io` (prod) — DNS here resolves to **internal AWS ALBs**
    (`internal-pipeline-prod-alb-1144201675.us-east-1.elb.amazonaws.com` → 172.30.x.x;
    TCP connect times out externally; i.e. this name is meant for in-VPC clients or the client
    rewrites it).
  - `https://2pipeline.s3.amazonaws.com` — the public S3 backing bucket.
  - `https://stg-pipeline.np.logitech.io` (staging), `https://updates.ghub.logitechg.com`
    (legacy/prod alias, CloudFront `d2l2wc59w4vrl3.cloudfront.net`, S3-backed).
- State model: depot channels hold `details.json` (next build) / `current.json` /
  `next.json` / `installation.json` / `version.json` / `features.json`;
  log strings: "Downloaded 'details.json' points to same build already installed (current: %s, next: %s)".
  Depot names referenced: `core`, `analytics`, `data`, `firmware`, `lghub`, `lghub_agent`,
  `lghub_system_tray`, `lghub_updater`, `lghub_installer`.
- Local roots: `%LOCALAPPDATA%/LGHUB/` (`icon_cache/`, `integrations/`, `integrations_config/`,
  `scripts/`), `LGHUBData`, `settings.db(-wal/-shm)`, `privacy_settings.db`, `unified_profiles`.
- Content container: **"capsule"** format (`logi::pipeline::CapsuleMetadata`) — JSON metadata
  blocks + resources, each resource header carries a **SHA field**; streamed depots;
  WebSocket++ transport also present.
- Auth: `Authorization: Bearer %s` / `auth=Bearer %s`; channel objects carry
  `pipeline_host`, `name`, `password`, `access_groups` (see §4). Client ids seen:
  `public`, `ghub13`, `com.logi.ghub`, GUID `{521c89be-637f-4274-a840-baaf7460c2b2}`,
  plus env switches `dev_cn_idaas` / `staging_cn_idaas` (Logi "idaas" identity stack) and
  `*_marketplace`, `*_device_recommendation`. Long 40-char tokens near the GUIDs
  (e.g. `e5fa04de24153837ce00d64e133a3be2q8d2Uz5cVYm`) are Sentry DSN keys, not pipeline creds.

### Endpoint verification performed (curl)

| Probe | Result |
|---|---|
| `https://download01.logi.com/.../lghub_installer.exe` | 200 OK (downloaded) |
| `https://2pipeline.s3.amazonaws.com/?list-type=2...` | 403 AccessDenied (listing disabled) |
| `.../core/LGHUB/details.json`, `/core/details.json`, `core/LGHUB/current.json`, object GETs | 403 AccessDenied (anonymous reads disabled → signed URLs or bearer-gated) |
| `pipeline.logitech.io` GET | connect timeout (internal-only LB from this network) |
| `updates.ghub.logitechg.com/{,check,manifest.json,details.json,current.json,builds,api/check,SoftwareManager/check}` | all 403 AccessDenied (S3) |
| Old full-installer filenames on download01 (`lghub_full*.exe`, `lghub_offline_installer.exe`, mac dmg variants) | 404 |

So: no anonymous URL fetches the app or datasets. The dataset for PID 0x40BD cannot be pulled
without going through the G HUB client (or capturing its authenticated pipeline traffic).
This is a verified negative; do not burn time brute-forcing S3 keys.

## 4. Plaintext protocol: complete protobuf schemas embedded in the binary

File-descriptor blobs sit in cleartext near offset ~13.72M of the exe. Recovered packages
(all proto3), with field names as embedded:

### logi.common_protocol / message.proto (`logi.protocol`)
- `Result {uint32 code, string what}`; `Code` enum: INVALID, INVALID_ARG, **INVALID_DEVICE**,
  NO_SUCH_PATH, CANCELLED, NOT_IMPLEMENTED, INVALID_VERB, NOT_READY, FAULTED, UNREACHABLE,
  UNAUTHORIZED, DUPLICATE_NAME, NOT_FOUND, EXCEPTION, CONFLICT, LUS_ERROR, SUCCESS.
- `Message {uint64 msg_id, Verb verb, string path, string origin, Result result, Any payload}`;
  `Verb`: GET, SET, BROADCAST, REMOVE, SUBSCRIBE, UNSUBSCRIBE, OPTIONS.
- `Routes.Route {verb, path, payload, example_json, endpoint}`, `RegistrationTokens {tokens}`.
  → G HUB services talk JSON-over-path RPC internally ("path" routing; `INVALID_DEVICE` proves
  device-scoped verbs).

### settings.proto (`logi.protocol.settings`)
- `KeyValue {key, created_time, last_modified_time, oneof value {bool_value, double_value,
  int_value, string_value}}`; `Event {action, KeyValue old_setting, KeyValue new_setting}`
  (UPDATED/ADDED/DELETED); `Error {code}` UNKNOWN/PERMISSIONS/**LOW_DISK_SPACE**.
  → every tunable G HUB exposes is a typed key-value with timestamps — including all
  per-device knobs (DPI/rate/actuation live here once installed).

### manifest.proto (`logi.protocol.manifest`) — the per-device payload carrier
- `Resource {key, src}`
- **`DeviceResource {model_id, resources[]}`** ← device-keyed resource lists
- `ReleaseNotes {content}`; `Manifest {Installer installer, DeviceResource[] devices,
  resources[], ReleaseNotes release_notes}`

### installer.proto (`logi.protocol.installer`)
- `Product {guid, name, publisher, icon, link_about, link_help, version}`
- `Shortcut {name, description, path, on_start_menu, on_desktop}`
- `CommandLine {executable, arguments, is_system}`; `CommandLineConditional {command,
  run_during_the_upgrade}`
- **`DriverPackage {package_name, drivers[], reboot_needed}`**;
  **`DriverPackage.Driver {Type type (INVALID_TYPE/INF/KEXT/PLUGIN), source, destination,
  version, hardware_id, recreate_device_nodes, restart_services}`** ← per-device driver
  targeting via `hardware_id` (classic VID/PID HWIDs for INFs)
- `ProcessIdentifier {process_name, type KILL_USING_LOGI_UTIL/KILL_USING_PROCESS_NAME/DONT_KILL}`;
  `ProcessControl{...}` / `ProcessControlEx {process_identifiers, lockdown_name}`
- `ConnectionWaitCheck {identifier, within_seconds, with_feature_flag}` (will_connect_to_lus_ipc
  — waits on LUS IPC)
- `Installer {product, installer/updater/uninstaller/user_uninstaller/user_repair CommandLine,
  process_control, files[], launchables[], driver_packages[], shortcuts[], detection,
  uninstall_commands[], regional_files[], extensions[], process_control_ex, will_connect_to_lus_ipc}`
- `Installer.Launchable {CommandLine command, bool service/as_agent/as_console_user/as_kernel,
  display_name, autostart, register_path, deferred, ConditionalMask conditional_mask,
  LaunchableType type, alias}`; LaunchableType: PRECONDITION, SYSTEM_SERVICE, USER_SERVICE,
  FRONTEND, ON_DEMAND_FRONTEND; ConditionalMask bit combos (INSTALL_BIT, UPDATE_BIT,
  RESTART_BIT, MANUAL/AUTO...)
- `Installer.File {source, destination, sha256, file_group, disable_folder_tracking, move,
  owner, group, mode, register_library}`
- `Detection {file_path, landmark_files, tag, WinRegistry, OSXBundle, SteamGame}` etc.;
  `DriverStore {driver_packages}`; `Installation {...full uninstall model, ExtensionsEntry}`

### updates.proto (`logi.protocol.updates`) — the CDN contract
- `Channel {pipeline_host, name, password, access_groups}`
- `PeriodicCheck {check_periodically, interval_in_seconds, next_check_due_date,
  download_automatically, next_check_due_timestamp}`
- `Signature {signature}` / `Signatures {version, signatures[]}` (+legacy form)
- **`Depot {name, State state, url, cipher_suite, key, mac, size, local_folder, files[],
  depends_on[], required, Signatures signatures}`** — each depot has symmetric crypto
  material (cipher_suite + key + mac) delivered in-band.
- `Depot.State`: INVALID→LOCKED→ABSENT→REQUESTED→DOWNLOADING_SIGNATURE→SIGNATURE_PRESENT→
  DOWNLOADING→DOWNLOADED→VERIFYING→VERIFIED→PRESENT
- `DownloadContext {build_id, depot_name, url, cipher_suite, key, mac, size, local_folder,
  downloaded_temp_file_path, files, Usage usage, signatures}` (Usage CURRENT/NEXT)
- `Depository {version, build_id, branch, access_group, region, Depot[] depots, lockdown_name,
  app_id}` (+Legacy, Reinstall with custom_tags, InstallStatus)
- `ContentRequest {Type type (FILE/ROOT_JSON_FILE), pipeline_url, local_url, content}`;
  `DepotList`, `PackageNameList {names}`
- Feature gating: `FeatureConditions {RegionCondition region, Flag[] flag_must_be_enabled,
  Platform platform_only, ArchitectureFilter platform_arch}` — Platform any/win/osx;
  ArchitectureFilter archs ANY/X64/ARM64; RegionCondition ALLOW/RESTRICT; `FeaturesCache
  {region, feature_flags}`; `FeatureConfig.Item {name, conditions, Actions actions
  {conditional_depots, add_feature_flags}}`
- `Key {name, version, last_modified, key}` / `AccessGroup` / `KeyStatus` FAILED/
  NO_NEW_KEYS/**FEATURE_KEYS_AVAILABLE**; `EnableUpdate`, `InstallUpdate {is_automatic}`,
  `Settings {settings_json}`

### depot_extensions.proto
- `DepotExecutable {pipeline_uri, arguments}`; `Extension {name, version, ...}`;
  `ExtensionState.error`; `DepotExtension {depot_name, extension_name}` — dynamic
  depot-downloaded executables/extensions (this is how `lghub_software_manager.exe` itself ships).

### pipl.proto (privacy) + crash_reporting.proto
- ConsentRequest/ConsentDetails {eula, pii_process, pii_transfer, pii_share};
  ConsentStatus DISABLED/ACCEPTED/REJECTED; RegionInfo {region, update_time}.
- crash_reporting Status/ManualEvent/UserInfo {user_id, user_name, lids_id, analytics_id,
  laclient_id}.

## 5. What this means for the X2 SUPERSTRIKE (PID 0x40BD)

1. `grep -i "superstrike|40bd"` across the installer: **zero meaningful hits** (the "prox"
   hits are "Proxy"). Expected: the installer ships no device data.
2. The authoritative per-device bundles are `DeviceResource {model_id, resources[{key,src}]}`
   entries inside per-release `Manifest`/`details.json` channel data on the pipeline —
   encrypted per depot (`cipher_suite/key/mac`) and Bearer-gated. `model_id` for this mouse
   is presumably its product string / HID PID ("40bd" or a model id) — to be confirmed from an
   installed system's captured manifest.
3. Installed-state layout (target files once a Windows/macOS install is available, or after
   running the client): `%PROGRAMDATA%\LGHUB\` + `%LOCALAPPDATA%\LGHUB\` — `settings.db`
   (SQLite, typed KeyValues per device), `unified_profiles`, `datasets` consumed by
   lghub_agent, `firmware/`, `scripts/`, `integrations(_config)`. Knob enumeration
   (DPI steps, polling-rate choices, actuation ranges with quantization, button-role table)
   lives in those datasets/settings, not in this installer.
4. Wire-level driver hint for Linux: Windows INF driver packages are distributed as
   `DriverPackage.Driver` with `hardware_id` strings (VID/PID HWIDs) and
   `recreate_device_nodes`/`restart_services` semantics — useful to learn which Win_INF IDs
   the Superstrike presents over the `046d:c54d` receiver pairing, if such a DriverPackage
   manifest is ever captured.

## 6. Reproduction commands (all static, no execution)

```bash
# hashes / container
sha256sum lghub_installer.exe
file lghub_installer.exe            # PE32+ ... 8 sections
7z l lghub_installer.exe            # Type=PE, resources, version.txt
7z x -oextracted lghub_installer.exe
# carve .text payloads
binwalk extracted/.text             # CAB/PE map (offsets in §2)
dd if=extracted/.text of=cab_25mb.cab bs=1 skip=30797061 count=24945422
bsdtar -tvf cab_25mb.cab            # a0..aN anonymized burn-container members
# protobuf + endpoint strings
strings -n 5 lghub_installer.exe | grep -oE '[A-Za-z0-9_/]+\.proto' | sort -u
strings -t d -n 4 lghub_installer.exe | grep -F 'pipeline://'
```

## 7. Sources / citations

- Official page listing both installers: https://www.logitechg.com/en-us/innovation/g-hub.html
- Installer (canonical): https://download01.logi.com/web/ftp/pub/techsupport/gaming/lghub_installer.exe
- macOS thin installer: https://download01.logi.com/web/ftp/pub/techsupport/gaming/lghub_installer.zip
- Pipeline hosts/behaviors, schemas: primary evidence = embedded strings/proto descriptors of
  the downloaded `lghub_installer.exe` (hash above), files under
  `downloads/ghub-installer/`
  (`extracted/`, `payload_pe1.bin`, `m_a1.bin`, `m_c1.bin`, `cab_inspect/`, carved CABs).
- WebSearch tool was unavailable this session (API 400); engine rotation attempted
  (bing/mojeek/marginalia/ddg-html) — only the official page fetch materially contributed.
