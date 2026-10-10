# Thick Thighs Save Lives

The I521 update applies Compact mode once and hides Main's Compact and Transparency controls. Settings retains density, transparency and each main-control visibility choice; subsequent loads preserve your choices and unknown saved settings.

---

**Help fund my AI overlords' coffee addiction so they can keep generating more plugins instead of taking over the world**

[☕ Support development on Ko-fi](https://ko-fi.com/mcvaxius)

[XA and I have created some Plugins and Guides here at -> aethertek.io](https://aethertek.io/)
### Repo URL:
```
https://aethertek.io/x.json
```

---

`TTSL` is a Dalamud plugin focused on keeping essential HUD state visible when your normal rendering setup is intentionally stripped down semi remote or remote.

Current surface:

- `/ttsl` main window
- `/ttsl setup`, `/ttsl wizard`, or `/ttsl guide` guided setup
- `/ttsl ws` window reset
- `/ttsl j` visible window jump
- zone and position snapshot
- HP / MP bars
- combat and duty condition flags
- repair summary
- party snapshot with HP / Mana / XYZ / distance
- party radar
- optional krangled display names

This repo is still under active development.

## Appearance

**Transparency** applies to the complete plugin window, including its titlebar and popups. Settings provides normal opacity, automatic focus fade, faded opacity and delay; defaults are 100%, fading to 50% after 10 seconds without focus and restoring on focus. Compact and language controls can be hidden independently on Main while remaining available in Settings.

Main's titlebar opens Settings or Guided Setup and toggles the local HUD overlay. The packaged icon appears in Main branding and its titlebar, including when collapsed. The setup wizard keeps its local/web HUD choices and permissions; enabling the local HUD does not grant remote control.

The main HUD now uses the approved two-column snapshot/remote/conditions/equipment and party/radar composition. Narrow windows stack the same groups and retain native scrolling. The header includes a compact `C` checkbox, colour picker, and language selector; Settings mirrors those preferences. Segoe UI content fonts use Dalamud's managed atlas with host CJK and symbol coverage. The plugin waits for required fonts instead of accepting a temporary host font as the finished design.

Hindi is enabled only when the local font check passes. Otherwise the selector shows disabled **Hindi (unavailable)** while other languages remain usable. A saved Hindi choice that fails its required-font check shows an English status and **Use English**; that button explicitly saves English. Font failures never change the saved language automatically.

English, German, French, Spanish, Italian, Russian, Japanese, Korean, Simplified Chinese, Vietnamese, Brazilian Portuguese, Indonesian, Polish, Turkish and Hindi resources are embedded in the plugin. The five additions appear as Tiếng Việt, Português (Brasil), Bahasa Indonesia, Polski and Türkçe after the original nine choices in the plugin, native server and generated browser HUD. Colour changes derive decorative surfaces, borders, text, and accents together; health and live/stale/disconnected meanings retain their own colours. Plugin appearance preferences use the existing configuration save path. Account-scoped publisher and permission settings keep their existing ownership.

The standalone native server has the same compact, colour, and language choices in its existing local configuration. Its Win32 controls resize with the window, wrap action rows, and show actual account/character/status/last-seen columns. The configured listening port supplies its HUD URL. The generated browser HUD stores its appearance choices beside its existing browser preferences and preserves Classic, Operator, Command, and Matrix modes, the five Inspector tabs, aggregate party rows, and source permission checks. `C` controls density; Show Details controls telemetry visibility. Both native and Python page producers contain the reviewed resource values.

Build and source checks do not establish visual acceptance. Actual game, native-window, and browser screenshots at the stated scale remain required before accepting the redesign. No font files are distributed. Legacy Debug output continues to be mirrored under `TTSL/bin/Debug` by the existing project target.

Local plugin builds require the sibling `aethertekUI` checkout and SDK 10.0.201. Enter its `eng/Enter-RepoEnv.ps1` environment and build `TTSL/TTSL.csproj` directly. The native route remains `cpp/build.bat [all|Debug|Release]`. GitHub builds check out the library beside TTSL using the repository's read-only `AETHERTEKUI_DEPLOY_KEY`; the existing public release keeps `latest.zip` and `latestServer.zip`.

## Setup Wizard

TTSL automatically opens a three-step setup wizard once for each fresh installation after a character is available. After that, you can reopen it from the main window, the settings window, or any setup command above.

The first step chooses **Local HUD**, **Local + Web**, or **Web only**. The second step selects the condition, repair, party, radar, Krangle, and DTR surfaces. The final step reviews the choices and, for web modes, shows the current server URL and copyable launch command.

The wizard keeps its choices in a draft until **Finish**. Canceling or closing a first-run wizard dismisses it for that account without changing the draft settings. Advanced web permissions, intervals, radar sizing, icon choices, and party-label settings remain under the full settings window.

## Native Server

The standalone Windows server is published as `latestServer.zip` on the [latest GitHub release](https://github.com/McVaxius/TTSL/releases/latest). Extract the archive and run `ttsl-native-server.exe`; no Python installation is required.

The **Remote HUD Server** settings section includes a **Download Native Server** button that opens the release page and identifies the `latestServer.zip` asset. The existing Python command remains available for users who prefer the script server.

## Python Server

The web HUD server lives under [server](Z:\TTSL\server).

Supported clean-machine setup:

```powershell
python -m pip install -r server\requirements.txt
python server\ttsl_server.py --host 127.0.0.1 --port 6942
```

Notes:

- The asset extractor now uses a bundled local copy of `luminapie` under `server\vendor\luminapie`.
- Researcher-only external folders are not required for normal runtime anymore.
- Generated runtime state stays local under:
  - `server\cache`
  - `server\extracted`
  - `server\_pydeps`
- `server\_pydeps` is still used as a fallback local cache if Python packages are missing, but the supported path is installing from `server\requirements.txt`.

## Support logs

Use **Copy / ZIP Dalamud log** in Settings > Settings to create a local ZIP and open its folder. At 100 MiB or above, the first click warns that logging may have stopped and recent activity may be missing; click **Export capped log anyway** only if you still want that snapshot. Share the ZIP manually and remove exports when no longer needed. **Open Export Folder** reopens the completed export’s folder.

When XA Slave is loaded, **Open XA Slave log tools** opens its **Utility > XA Mods** panel, which contains Dalamud Log Cleaner. The existing **Copy / ZIP Dalamud log** action remains separate. Opening the panel does not run cleanup or change XA Slave settings.
