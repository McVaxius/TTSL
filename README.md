# Thick Thighs Save Lives

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
