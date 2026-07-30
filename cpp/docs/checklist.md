# TTSL Native C++ Final Test Checklist

Use this checklist against the C++ Debug application:

```text
D:\.AI\FFXIV\.External Plugins\TTSL-master\cpp\build\Debug\ttsl-native-server.exe
```

Expected HUD URL:

```text
http://127.0.0.1:6942/
```

## Legend
- [ ] Needs your live test
- [X] Codex verified in the local smoke test
- [R] Known issue outside the C++ server scope

## Release Packaging

- [X] Run `cpp\build.bat Release`.
- [X] Create `TTSL\bin\x64\Release\TTSL\latestServer.zip` with the workflow's `Compress-Archive` command.
- [X] Confirm the archive contains exactly one root entry named `ttsl-native-server.exe`.
- [X] Confirm the parsed release workflow keeps `TTSL.json` in the private build artifact and publicly attaches exactly `latest.zip` and `latestServer.zip`.
- [ ] Publish `v0.0.1.13`, confirm GitHub exposes exactly `latest.zip` and `latestServer.zip`, download both, confirm `latest.zip` contains `TTSL.json`, and confirm `latestServer.zip` contains only one root `ttsl-native-server.exe`.
- [ ] In plugin settings, click **Download Native Server** and confirm the latest release page opens and lists `latestServer.zip`.

## Test Order For Your Live Pass

- [ ] Launch `cpp\build\Debug\ttsl-native-server.exe` and confirm no command terminal opens.
- [ ] Confirm the taskbar/title-bar icon uses the embedded custom icon.
- [ ] Start FFXIV with the plugin and confirm the native app shows the live client.
- [ ] Open `http://127.0.0.1:6942/` and confirm live telemetry renders in the HUD.
- [ ] Confirm `/api/state.assetPlan.samePcCaptured` is `true` from the plugin-provided `gameInstallPath`.
- [ ] Confirm asset-plan counts populate for job icons, maps, races, and tribes.
- [ ] Click `Extract Assets` and confirm the extraction message advances through active work such as EXD loading, job icon extraction, map texture extraction, race icon generation, tribe icon generation, and summary writing.
- [ ] Confirm extraction finishes with `lastExitCode = 0` and `0` failed files.
- [ ] Confirm job PNGs, map PNGs, race SVGs, and tribe SVGs render in the HUD.
- [ ] Test plain text echo input and confirm the draft does not reset while the HUD refreshes.
- [ ] Test screenshot and CCTV request/upload behavior, with the known plugin-side overlay limitation below.
- [ ] Confirm Lodestone cache behavior for a real character.
- [ ] Close the app and confirm no `ttsl-native-server.exe` process remains.

## Codex Verified Build And Smoke Test

- [X] Debug build succeeded.
- [X] Release build succeeded.
- [X] Native app launches as a Win32 GUI app, not a command terminal.
- [X] Native executable icon resource was embedded from `cpp\icon\ttsl-native-server.ico`.
- [X] `GET /` returned HTTP 200.
- [X] Served HUD JavaScript passed `node --check`.
- [X] Sample `POST /api/update` succeeded.
- [X] `/api/state` returned `totalClients = 1` for sample telemetry.
- [X] `/api/state` included `client.lodestone`.
- [X] `/api/state` included aggregate-party `sourceLodestone`.
- [X] Lodestone metadata cache wrote under `cpp\cache\lodestone`.
- [X] `/assets/job-icons/62021.png` returned HTTP 200.
- [X] `/assets/maps/12_s1t201_m.png` returned HTTP 200.
- [X] Native EXD race/clan naming loaded from local FFXIV data.
- [X] Full native extraction with a real local game path produced 4 extracted, 0 failed.
- [X] `/assets/job-icons/62041.png` returned HTTP 200.
- [X] `/assets/maps/12_s1t201_m.png` returned HTTP 200.
- [X] `/assets/race-icons/race_6.svg` returned HTTP 200.
- [X] `/assets/tribe-icons/tribe_12.svg` returned HTTP 200.
- [X] Extraction status now advances during slower job/map extraction stages.
- [X] Test process was stopped by exact PID, with no stray native server process left.

## App Startup

- [ ] Launch `ttsl-native-server.exe`.
- [ ] Confirm no command terminal window opens.
- [ ] Confirm the native Windows UI opens.
- [ ] Confirm the window and taskbar show the custom app icon.
- [ ] Confirm the server auto-starts on `127.0.0.1:6942`.
- [ ] Confirm the UI shows `Server running at http://127.0.0.1:6942/`.
- [ ] Click `Open HUD` and confirm the browser HUD opens.
- [ ] Click `Stop Server` and confirm the HUD stops responding.
- [ ] Click `Start Server` and confirm the HUD responds again.
- [ ] Close the native UI and confirm the server stops.

## Native App Management UI

- [ ] Confirm the active-client list updates as plugin clients connect.
- [ ] Confirm active-client rows show live/stale/offline status, age, job, and zone.
- [ ] Change host, port, or stale timeout, close/reopen, and confirm settings persist.
- [ ] Set a custom `Data folder`, close/reopen, and confirm `/api/state.assetExtraction.summaryPath` points under it.
- [ ] Click `Open Data` and confirm the active data folder opens.
- [ ] Click `Reset Default`, close/reopen, and confirm the data folder returns to `%LOCALAPPDATA%\TTSL Native Server`.
- [ ] Click `Cache` and confirm the configured data folder's `cache` folder opens.
- [ ] Click `Extracted` and confirm the configured data folder's `extracted` folder opens.
- [ ] Click `Copy URL` and confirm the HUD URL is on the clipboard.
- [ ] Click `Diagnostics` and confirm diagnostics are copied to the clipboard.
- [ ] Click `Clear Stale` and confirm stale/disconnected clients are removed.
- [ ] Click `Clear Cache` and confirm cache files are cleared and the app keeps running.
- [ ] Click native `Extract Assets` after a same-PC game path is captured and confirm extraction starts.
- [ ] Confirm the native extraction status text changes during long job-icon or map-texture extraction instead of staying on the metadata message.

## Plugin Connection And State

- [ ] Start at least one FFXIV client with the plugin sending telemetry to the native server.
- [ ] Confirm the native UI tracked-client count increases.
- [ ] Open `/api/state` and confirm JSON is returned.
- [ ] Confirm each client has `characterName`, `worldName`, `accountId`, `lastSeenUtc`, and `ageSeconds`.
- [ ] Confirm each client has `lodestone`.
- [ ] Confirm stale clients change status after the configured stale timeout.
- [ ] Confirm disconnected clients remain visible until retention cleanup.
- [ ] Confirm party-linked clients appear in `aggregateParties`.
- [ ] Confirm non-party clients appear in `looseClients`.
- [ ] Confirm the native app window Krangle checkbox only changes native client-list names and persists across app restarts.

## HUD Modes

- [ ] Open the HUD at `http://127.0.0.1:6942/`.
- [ ] Confirm the native browser HUD exposes the Python HUD layout controls, detail toggle, aggregate parties, map sizing, and web Krangle toggles.
- [ ] Confirm `Classic` mode renders client cards.
- [ ] Confirm `Operator` mode renders the left rail and selected detail panel.
- [ ] Confirm `Command` mode renders command-oriented cards.
- [ ] Confirm `Matrix` mode renders dense rows.
- [ ] Toggle `Show stale` and confirm stale/disconnected clients hide/show.
- [ ] Toggle `Aggregate parties` and confirm party grouping changes.
- [ ] Toggle `Details` and confirm threat/map/detail sections hide/show.
- [ ] Toggle `Icons` and confirm extracted/generated icons show/hide.

## Client And Party Data

- [ ] Confirm HP and MP display when player telemetry is present.
- [ ] Confirm repair data displays when repair telemetry is present.
- [ ] Confirm territory/map name displays when zone telemetry is present.
- [ ] Confirm position displays when position telemetry is present.
- [ ] Confirm condition flags affect the flow label, such as combat, duty, queue, mount, cast, or dead.
- [ ] Confirm party rows render when party telemetry is present.
- [ ] Confirm monitored party members are marked from tracked clients.
- [ ] Confirm unmonitored party members still appear as party rows.
- [ ] Confirm threat rows render when combat telemetry is present.
- [ ] Confirm source-client routing works for aggregate-party remote actions.

## Assets And Map Rendering

- [ ] Confirm `/api/state.assetPlan` exists.
- [ ] Confirm `/api/state.assetPlan.samePcCaptured` becomes `true` when a same-PC client reports `gameInstallPath`.
- [ ] Confirm `/api/state.assetPlan.summary.jobIcons` increases when job icon IDs are reported.
- [ ] Confirm `/api/state.assetPlan.summary.maps` increases when map texture telemetry is reported.
- [ ] Confirm `/api/state.assetPlan.summary.races` and `tribes` increase when race/tribe IDs are reported.
- [ ] Confirm `/api/state.assetExtraction.message` reports auto extraction after same-PC telemetry provides `gameInstallPath` and requested assets are missing.
- [ ] Confirm generated browser assets older than 24 hours are treated as stale and refreshed under the configured data folder.
- [ ] Click `Extract Assets`.
- [ ] Confirm the button changes to `Extracting...`.
- [ ] Confirm `/api/state.assetExtraction.running` becomes `true`.
- [ ] Confirm `/api/state.assetExtraction.message` advances through active stages, including EXD loading, job icon extraction, map texture extraction, race icon generation, tribe icon generation, and summary writing.
- [ ] Wait for extraction to finish.
- [ ] Confirm `/api/state.assetExtraction.running` returns to `false`.
- [ ] Confirm `/api/state.assetExtraction.lastExitCode` is `0` for a clean extraction.
- [ ] Confirm the configured data folder's `extracted\ttsl_asset_extract_summary.json` is written.
- [ ] Confirm `ttsl_asset_extract_summary.json` has `status = "ok"` and an empty `failedFiles` array for a clean extraction.
- [ ] Confirm job icon PNGs are written under the configured data folder's `extracted\generated\job-icons`.
- [ ] Confirm map PNGs are written under the configured data folder's `extracted\generated\maps`.
- [ ] Confirm race SVGs are written under the configured data folder's `extracted\generated\race-icons`.
- [ ] Confirm tribe SVGs are written under the configured data folder's `extracted\generated\tribe-icons`.
- [ ] Confirm extracted race/tribe summary entries show `nameSource = "exd"` when local EXD data is available.
- [ ] Confirm race/clan names in the summary match the live character's race/clan instead of generic `Race N` or `Tribe N` labels.
- [ ] Open a job icon `/assets/...png` URL and confirm HTTP 200.
- [ ] Open a map `/assets/...png` URL and confirm HTTP 200.
- [ ] Open a race icon `/assets/...svg` URL and confirm HTTP 200.
- [ ] Open a tribe icon `/assets/...svg` URL and confirm HTTP 200.
- [ ] Confirm the HUD renders job/race/tribe icons.
- [ ] Confirm the HUD renders extracted map PNGs in map/radar panes.
- [ ] Confirm player, party, and hostile points draw on the map/radar overlay.

## Lodestone Portrait Cache

- [ ] Confirm `/api/state.accountGroups[].clients[].lodestone.status` appears.
- [ ] Confirm `/api/state.aggregateParties[].sourceLodestone.status` appears for aggregate parties.
- [ ] Confirm `/api/state.aggregateParties[].members[].lodestone.status` appears for party members.
- [ ] Wait for a real character lookup to resolve.
- [ ] Confirm the configured data folder's `cache\lodestone` contains a per-character folder with `metadata.json`.
- [ ] If the lookup resolves, confirm `face.*` and/or `portrait.*` files are cached.
- [ ] If the lookup resolves, confirm the HUD portrait frame uses the cached image instead of initials.
- [ ] If the lookup does not resolve, confirm the HUD keeps initials and `/api/state` reports `not_found` or `error` without breaking the page.
- [ ] Click `Clear Cache` and confirm Lodestone images/metadata are removed.
- [ ] Confirm a later `/api/state` queues fresh Lodestone lookups again.

## Remote Actions

- [ ] For a client that allows screenshot requests, click `Screenshot`.
- [ ] Confirm the next plugin update consumes a queued `requestScreenshot` action.
- [ ] For a client that allows CCTV streaming, click `CCTV`.
- [ ] Confirm the next plugin update consumes a queued CCTV screenshot action.
- [ ] For a client that allows echo commands, type plain text and click `Send`.
- [ ] Confirm the text field does not reset while you type.
- [ ] Confirm the next plugin update consumes a queued `echoCommand` action.
- [ ] Confirm disabled buttons appear when the client policy does not allow the action.

## Screenshot And CCTV Uploads

- [ ] Trigger a screenshot request from the HUD.
- [ ] Confirm the plugin uploads a screenshot to the native server.
- [ ] Confirm the HUD shows a `Last screenshot` link.
- [ ] Click the `Last screenshot` link and confirm the image loads.
- [ ] Trigger a CCTV frame request.
- [ ] Confirm the HUD shows a `Last CCTV` link.
- [ ] Click the `Last CCTV` link and confirm the image loads.
- [ ] Confirm uploaded files are stored under the configured data folder's `cache\screenshots` or `cache\cctv`.
- [R] Overlay-free screenshot/CCTV capture requires a plugin-side capture implementation change outside the C++ server.

## File And Process Hygiene

- [ ] Confirm no unwanted command terminal is left open.
- [ ] Confirm no stray `ttsl-native-server.exe` process remains after closing the app.
- [ ] Confirm runtime artifacts remain under the configured data folder's `cache`, `extracted`, or `ttsl_asset_plan.json` paths.
- [ ] Confirm build output remains under `cpp\build`.
- [ ] Confirm no work happened in `Z:`.

## Remaining Outside C++ Scope

- [R] Plugin-side screenshot/CCTV capture still uses the plugin upload source; overlay-free game-only capture must be fixed in the plugin.
- [R] Plugin-side config UI may still mention the Python launch command; this was not changed because this rollout stayed inside `cpp`.
