# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Requires `PLUGIN_SDK_DIR` environment variable pointing to the GTA III Plugin SDK. Optionally set `GTA_III_DIR` for automatic deploy to the game's `scripts/` folder.

```bash
# Release build (recommended for testing in-game)
.\build.bat

# Debug build (includes .pdb symbols; debugger configured to launch gta3.exe)
.\build_debug.bat
```

Both scripts auto-discover MSBuild via `vswhere`, kill `gta3.exe` if running, and copy the output `.asi` to the game directory. The output artifact is `Autosave.III.asi`.

To build manually via Visual Studio: open `Autosave.sln`, select **Debug GTA3** or **Release GTA3** configuration (platform is **GTA3**, not the usual x86/x64).

If the Plugin SDK is missing, clone it:
```bash
git clone https://github.com/DK22Pac/plugin-sdk
```
Then set `PLUGIN_SDK_DIR` to that directory.

## CI / Releases

`.github/workflows/build.yml` runs on every push to master. It clones the Plugin SDK, builds the Release configuration, and publishes a GitHub release tagged `v1.0.{N}` where N is the total commit count. Both `Autosave.III.asi` and `Autosave.III.ini` are attached as release assets. Release notes are auto-generated from commits by GitHub.

The post-build event in the vcxproj (which kills `gta3.exe` and copies to the game dir) is guarded by `if defined GTA_III_DIR` and does nothing in CI.

## Architecture

Everything lives in a single file: `source/Main.cpp`. It is organized into three logical layers:

1. **`Config` namespace** — compile-time constants (cooldowns, ranges, save slots). Change behavior here first before touching logic.
2. **`Utils` namespace** — stateless helper functions. Includes game-state queries (`IsOnMission`, `IsCutsceneRunning`, `IsGameSafeToSave`) and blip/marker utilities. `IsMissionGiverSprite` hard-codes the list of GTA III mission giver radar sprites used to distinguish mission markers from other blips.
3. **`AutosaveMod` class** — singleton that owns all mutable state and hooks into three Plugin SDK events:
   - `OnGameInit` — loads config from `Autosave.III.ini`
   - `OnGameProcess` — per-frame logic: load detection, autosave triggering, mission retry state machine
   - `OnDrawHud` — renders the "Autosaved" notification, retry prompt, and debug overlay

### Key design details

- **Two independent save slots**: slot 7 is the "approach" autosave (written when entering a mission marker radius, used as the retry restore point); slot 6 is the "mission complete" autosave. They have separate cooldown timers.
- **Time preservation**: GTA III advances the in-game clock by 6 hours on every save. `PerformAutosave()` snapshots and restores `CClock` state around the save call to prevent this.
- **Load detection**: There is no explicit "load" callback in the Plugin SDK. Loads are detected by monitoring `CTimer` for a wraparound/reset (`m_lastGameTime` vs current game time).
- **Mission failure detection**: Checked by scanning the big-message text buffer for the strings `"MISSION FAILED"` or `"M_FAIL"`, since there is no dedicated API for this.
- **Post-load rotation**: After a detected load, the mod rotates the player and camera toward the nearest mission blip (within `MISSION_BLIP_ROTATION_RANGE`). A 500 ms grace period (`POST_LOAD_GRACE_PERIOD_MS`) suppresses the autosave trigger immediately after rotation so the player isn't auto-saved the instant they load.

### Adding new behavior

1. Add any new constants to the `Config` namespace.
2. Add state variables as `m_`-prefixed members of `AutosaveMod`.
3. Wire logic into `OnGameProcess` (state changes) or `OnDrawHud` (rendering).
4. Extract reusable queries into `Utils`.

## Debugging

Set `Debug = 1` in `Autosave.III.ini`. This enables an on-screen overlay (top-left) showing live state: proximity to marker, load detection, missions-passed count, on-mission flag, and mission-failed-text visibility. No rebuild required — the ini is read at game init.
