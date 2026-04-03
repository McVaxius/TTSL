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
