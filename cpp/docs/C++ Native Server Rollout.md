# TTSL C++ Native Server Rollout

Last updated: 2026-05-06 20:24:00

## Goal

Replace the Python remote HUD server with a native Windows C++ application that has a real UI, preserves the plugin HTTP contract, and reaches feature parity with the Python data and browser HUD flow.

The implementation and build output stay under:

```text
D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp
```

## Current Rollout State

The C++ application is current through Phase 6 for the C++-only scope. The latest additions were:

- Native EXD reading for localized race and clan names.
- Full native asset extraction verified against a real local FFXIV install path.
- Live extraction progress messages for metadata, job icons, map textures, race icons, tribe icons, and summary writing.
- Custom executable/window icon embedded from `cpp\icon\ttsl-native-server.ico`.
- Debug and Release rebuilds of `ttsl-native-server.exe`.

Codex has verified the HTTP contract, HUD load, JavaScript syntax, sample telemetry, Lodestone cache shape, generated asset serving, native EXD names, and a full local extraction producing job PNG, map PNG, race SVG, and tribe SVG files.

The remaining validation is your live operator pass with the real plugin sending telemetry from FFXIV.

## Phase 0 - Native Shell And HTTP Contract

Status: complete

- Native Win32 GUI app, no console subsystem.
- Start/Stop/Open HUD/Screenshots controls.
- HTTP listener on `127.0.0.1:6942`.
- Plugin endpoints:
  - `POST /api/update`
  - `POST /api/goodbye`
  - `GET /api/state`
  - `POST /api/queue-action`
  - `POST /api/upload-screenshot`
  - `POST /api/open-screenshot-folder`
  - `GET /assets/...`
- Verified with smoke tests for update, state, action queue, screenshot upload, and asset serving.

## Phase 1 - Data Snapshot Parity

Status: complete for the first native parity pass

Port the Python server data shaping that the full HUD expects:

- Build `assetPlan` from incoming client, party, map, job, race, tribe, and enemy telemetry.
- Build `assetCatalog` from the configured native `extracted` and `cache` folders.
- Preserve same-PC game path capture in `gamePathInfo`.
- Aggregate clients into party surfaces:
  - `aggregateParties`
  - `looseClients`
  - source policy, source screenshot, and source CCTV metadata
- Keep remote action and screenshot semantics compatible with the Dalamud plugin.

Implemented in this pass:

- `assetPlan` now derives territory, map, race, tribe, job, job-icon, map-texture, and enemy IDs from incoming client snapshots.
- `ttsl_asset_plan.json` is generated as a runtime artifact under the configured data folder.
- `assetCatalog` now reads `extracted\ttsl_asset_extract_summary.json` when present and exposes browser-ready extracted files through the configured cache folder.
- Party-linked clients are grouped into `aggregateParties`.
- Non-represented clients remain in `looseClients`.
- Same-PC game path metadata is carried through `gamePathInfo`.

## Phase 2 - Browser HUD Parity

Status: implemented for the current native HUD rollout

Port the Python browser HUD behavior into the C++ served page:

- Layout modes:
  - Classic
  - Operator
  - Command
  - Matrix
- Header controls:
  - Show details
  - Show stale/disconnected
  - Aggregate parties
  - Icons toggle
  - Radar/map box sizing
  - Combat/travel yalm sizing
- Client cards with:
  - HP/MP/repair/status flags
  - party list
  - threat/enmity list
  - minimap/radar canvas
  - screenshot/CCTV/text controls
- Party aggregate boards with source-client command routing.

Implemented in this pass:

- Classic, Operator, Command, and Matrix layout modes.
- Header toggles for stale clients, aggregate parties, and detail sections.
- Header toggle for icon/image rendering.
- Overview metrics for clients, party surfaces, asset-plan counts, and game-path capture.
- Rich client cards with HP, MP, repair, position, flow state, party rows, threat rows, remote actions, and radar canvas placeholders.
- Aggregate party cards with monitored/stranger member counts, source-client remote routing, threat rows, and radar canvas placeholders.
- Matrix rows for dense scan mode.
- Job, race, and tribe icons now render from `assetCatalog` when extracted/generated files are available.
- Map textures now render from `assetCatalog` with FFXIV map-coordinate projection.
- Map/radar overlays now draw source, party, and hostile points with facing indicators.
- Plain text command drafts are preserved while the HUD refreshes.

Remaining browser HUD parity work:

- No remaining Python HUD parity item is known inside the C++ server scope.

## Phase 3 - Native Asset Pipeline

Status: implemented for native pipeline rollout

Replace the Python extractor path with native C++ equivalents:

- Read `ttsl_asset_plan.json` equivalent data from memory or disk.
- Extract job icons and map textures from the FFXIV install.
- Convert `.tex` data to browser-viewable images.
- Generate race/clan SVG or PNG icons with official EXD names when available and fallback labels otherwise.
- Write `ttsl_asset_extract_summary.json` equivalent output.
- Continue serving extracted files through `/assets/...`.

Implemented in this pass:

- `/api/extract-assets` now starts a native background extraction worker.
- `/api/state` now exposes live `assetExtraction` status, timestamps, exit code, and summary path.
- The served HUD has an `Extract Assets` button that disables while extraction is running or while no same-PC game path has been captured.
- Native SQPACK index/data reading was added for FFXIV asset paths.
- Native raw deflate handling was added with local `miniz` sources under `cpp\third_party\miniz`.
- Native TEX conversion was added for:
  - A8R8G8B8/BGRA
  - DXT1/BC1
  - DXT3/BC2
  - DXT5/BC3
- Native PNG writing was added through Windows Imaging Component.
- Race and tribe SVG icons are generated without Python.
- Native EXD sheet reading loads localized race/clan names for generated race and tribe icons.
- Asset extraction progress now advances through metadata, job-icon, map-texture, race-icon, tribe-icon, and summary-writing stages.
- Extraction writes `extracted\ttsl_asset_extract_summary.json` under the configured data folder.
- Browser-ready generated assets continue to flow through `assetCatalog` and `/assets/...`.

Remaining Phase 3 validation:

- Codex local smoke validation passed with a real local game path.
- Your live pass should confirm the plugin-provided `gameInstallPath`, live job/map/race/tribe telemetry, native UI `Extract Assets` button, HUD `Extract Assets` button, generated summary, and in-HUD rendering all line up during real gameplay.

## Phase 4 - Lodestone Portrait Cache

Status: implemented for the current native C++ rollout

Port or replace Python Lodestone visual enrichment:

- Search Lodestone by character/world.
- Cache metadata and downloaded face/body images.
- Add `lodestone` objects to clients, party members, aggregate party sources, and aggregate members.
- Respect cache TTL and avoid duplicate concurrent lookups.

Risk: this uses live web scraping and should remain resilient to Lodestone markup changes.

Current state:

- Native WinHTTP search/download/cache is live.
- Metadata and downloaded images are cached under the configured data folder's `cache\lodestone`.
- `/api/state` now emits:
  - `client.lodestone`
  - `party.sourceLodestone`
  - `party.members[].lodestone`
- The HUD portrait frame uses `faceUrl`/`portraitUrl` when available and falls back to initials while a lookup is pending, not found, or errored.
- Duplicate in-flight lookups for the same character/world are suppressed.
- Cache clear also resets the in-memory Lodestone metadata view.

## Phase 5 - Native App Management UI

Status: implemented for the current native app rollout

Expand the native window beyond basic status:

- Active clients table.
- Bind host/port/stale settings persisted to a config file.
- Runtime asset extraction status.
- Open data/extracted/cache folders.
- Copy URL and server diagnostics.
- Clear stale clients and clear cache buttons.

Implemented in this pass:

- Native window now has data-folder, cache, extracted-assets, copy URL, diagnostics, clear stale, clear cache, and extract-assets controls.
- Active clients are listed in the native window with live/stale/offline status, age, job, and zone.
- Host, port, stale timeout, and configured data root persist to `%LOCALAPPDATA%\TTSL Native Server\ttsl-native-config.json`.
- Runtime cache, extracted assets, and `ttsl_asset_plan.json` live under the configured data root.
- Native web HUD now mirrors the Python HUD controls and starts extraction automatically when same-PC telemetry requests missing or 24-hour-stale browser assets.
- Native and web Krangle toggles are independent: the native checkbox affects the local Windows client list, while web toggles stay in browser local storage.
- Native management API endpoints were added for cache/extracted folder opens and stale/cache clearing.

## Phase 6 - Packaging And Plugin Integration

Status: complete for the C++ application scope

Prepare the native app for normal TTSL use:

- Release build output.
- Optional plugin UI copy/open command changes from Python launch command to native executable path.
- README update after native parity is stable.
- Verify no command terminal opens when launched normally.

Implemented in this pass:

- Debug build verified at `cpp\build\Debug\ttsl-native-server.exe`.
- Release build verified at `cpp\build\Release\ttsl-native-server.exe`.
- The native executable remains a Win32 GUI app and does not open a command terminal.
- The native executable and window class use the embedded `ttsl-native-server.ico` resource.

Remaining Phase 6 work:

- Plugin-side config window still references the Python launch command. Per current scope, this pass did not edit the plugin project outside `cpp`.
- Overlay-free screenshot/CCTV capture needs a plugin-side capture-path change; the C++ server can only receive and serve what the plugin uploads.

## Operator Live Test Priorities

Use [checklist.md](checklist.md) for the detailed checklist. The highest-signal pass is:

1. Launch `cpp\build\Debug\ttsl-native-server.exe` and confirm no command terminal opens.
2. Confirm the taskbar/title-bar icon uses the embedded custom icon.
3. Start FFXIV with the plugin and confirm the native UI active-client list updates from real telemetry.
4. Open `http://127.0.0.1:6942/` and confirm Classic, Operator, Command, and Matrix modes render live data.
5. Confirm `/api/state.assetPlan.samePcCaptured = true` and that job icon, map, race, and tribe counts populate from the live client.
6. Click `Extract Assets` and confirm status advances through active extraction messages instead of appearing stuck.
7. Confirm the configured data folder's `extracted\ttsl_asset_extract_summary.json` reports `status = ok`, `lastExitCode = 0`, and no failed files for a clean extraction.
8. Confirm job icons, map PNGs, race SVGs, tribe SVGs, and map/radar overlays render in the HUD.
9. Test echo text input, screenshot request, and CCTV request from the HUD.
10. Confirm Lodestone portraits/cache behavior for a real character.
11. Close the native app and confirm no stray `ttsl-native-server.exe` process remains.

## Current Build Command

```powershell
cmake -S "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp" -B "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" -G "Visual Studio 17 2022" -A x64
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Debug --parallel
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Release --parallel
```

## Current Verification Command Shape

Use the native executable only by PID when test-stopping it. Do not kill all matching process names.

```powershell
$p = Start-Process -FilePath "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build\Debug\ttsl-native-server.exe" -PassThru
try {
  Invoke-RestMethod -Uri "http://127.0.0.1:6942/api/state"
}
finally {
  if ($p -and -not $p.HasExited) {
    Stop-Process -Id $p.Id -Force
  }
}
```

## 2026-05-06 Verification

### Phase 3 Native Asset Pipeline

Command set:

```powershell
cmake -S "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp" -B "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" -G "Visual Studio 17 2022" -A x64
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Debug --parallel
```

Build result:

- `ttsl-native-server.exe` built successfully in `cpp\build\Debug`.

Smoke test result:

- Native GUI executable started on `127.0.0.1:6942`.
- Posted one same-PC client snapshot with temporary fake `game\sqpack` root, `raceId = 1`, and `tribeId = 3`.
- `POST /api/extract-assets` started the native worker.
- `/api/state` reported:
  - `assetExtraction.message = "Native asset extraction ok: 2 extracted, 0 failed."`
  - `assetExtraction.lastExitCode = 0`
  - `assetCatalog.raceIcons["1"].svgUrl = /assets/race-icons/race_1.svg`
  - `assetCatalog.tribeIcons["3"].svgUrl = /assets/tribe-icons/tribe_3.svg`
- `GET /assets/race-icons/race_1.svg` returned HTTP 200.
- `GET /assets/tribe-icons/tribe_3.svg` returned HTTP 200.
- Test artifacts were removed after the smoke test.

Install-path check:

- Common local FFXIV install paths under `C:\Program Files`, Steam, `D:\SquareEnix`, `D:\Games`, and Steam library folders did not contain `game\sqpack` on this machine.
- Real SQPACK texture extraction should be validated with live plugin telemetry once a same-PC game path is available.

### Phase 1/2 Data And HUD

Command set:

```powershell
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Debug --parallel
```

Smoke test result:

- Native GUI executable started on `127.0.0.1:6942`.
- Posted two party-linked client snapshots.
- `/api/state` returned:
  - `totalClients = 2`
  - `aggregateParties = 1`
  - `looseClients = 0`
  - `assetPlan.summary.jobIcons = 2`
  - `assetPlan.summary.maps = 1`
  - `assetPlan.summary.enemies = 2`
- `GET /` returned HTTP 200 for the richer HUD.
- `POST /api/queue-action` succeeded.
- The next `POST /api/update` consumed one queued action.

### Phase 2/5/6 Native HUD And App Management Follow-Up

Command set:

```powershell
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Debug --parallel
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Release --parallel
```

Build result:

- `ttsl-native-server.exe` built successfully in `cpp\build\Debug`.
- `ttsl-native-server.exe` built successfully in `cpp\build\Release`.

Smoke test result:

- Native GUI executable started on `127.0.0.1:6942`.
- `GET /` returned HTTP 200.
- The embedded HUD JavaScript passed `node --check`.
- Posted one sample telemetry payload with job, party, map, position, policy, and hostile data.
- `/api/state` returned `totalClients = 1`.
- `assetCatalog.available = true`.
- `/assets/job-icons/62010.png` returned HTTP 200.
- `/assets/maps/12_s1t201_m.png` returned HTTP 200.
- Test process was stopped by exact PID after verification.

### Phase 4 Lodestone Native Cache Follow-Up

Command set:

```powershell
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Debug --parallel
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Release --parallel
```

Build result:

- `ttsl-native-server.exe` built successfully in `cpp\build\Debug`.
- `ttsl-native-server.exe` built successfully in `cpp\build\Release`.

Smoke test result:

- Native GUI executable started on `127.0.0.1:6942`.
- `GET /` returned HTTP 200.
- The embedded HUD JavaScript passed `node --check`.
- Posted one sample telemetry payload with client, party, map, position, policy, and hostile data.
- `/api/state` returned `totalClients = 1`.
- The sample client exposed `lodestone.status = not_found` after the fake-character lookup completed.
- The aggregate party exposed `sourceLodestone`.
- `assetCatalog.available = true`.
- `/assets/job-icons/62021.png` returned HTTP 200.
- `/assets/maps/12_s1t201_m.png` returned HTTP 200.
- Test process was stopped by exact PID after verification.

### Phase 3 EXD And Full Asset Follow-Up

Command set:

```powershell
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Debug --parallel
cmake --build "D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build" --config Release --parallel
```

Build result:

- `ttsl-native-server.exe` built successfully in `cpp\build\Debug`.
- `ttsl-native-server.exe` built successfully in `cpp\build\Release`.

Smoke test result:

- Native GUI executable started on `127.0.0.1:6942`.
- Posted one same-PC client snapshot using `E:\Steam\steamapps\common\FINAL FANTASY XIV Online\game`.
- `/api/state.assetPlan.summary` reported 1 job icon, 1 map, 1 race, and 1 tribe.
- `POST /api/extract-assets` completed with `assetExtraction.lastExitCode = 0`.
- `ttsl_asset_extract_summary.json` reported `status = ok`, `extracted = 4`, and `failed = 0`.
- EXD names resolved for `raceId = 6` as `Au Ra` and `tribeId = 12` as `Xaela`.
- `metadataWarnings` was empty.
- Live extraction status advanced to active job-icon and map-texture messages instead of staying on the EXD metadata message.
- `/assets/job-icons/62041.png` returned HTTP 200.
- `/assets/maps/12_s1t201_m.png` returned HTTP 200.
- `/assets/race-icons/race_6.svg` returned HTTP 200.
- `/assets/tribe-icons/tribe_12.svg` returned HTTP 200.
- Test process was stopped by exact PID after verification.
