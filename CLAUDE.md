# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Requires `PLUGIN_SDK_DIR` environment variable pointing to the Plugin SDK. Optionally set `GTA_III_DIR`, `GTA_VC_DIR`, or `GTA_SA_DIR` for automatic deploy to the respective game's `scripts/` folder.

### GTA III

```bash
# Release build (recommended for testing in-game)
.\build.bat

# Debug build (includes .pdb symbols; debugger configured to launch gta3.exe)
.\build_debug.bat
```

Both scripts auto-discover MSBuild via `vswhere`, kill `gta3.exe` if running, and copy the output `.asi` to the game directory. The output artifact is `Autosave.III.asi`.

### Vice City

```bash
# Release build (recommended for testing in-game)
.\build_vc.bat

# Debug build (includes .pdb symbols; debugger configured to launch gta-vc.exe)
.\build_vc_debug.bat
```

Both scripts auto-discover MSBuild via `vswhere`, kill `gta-vc.exe` if running, and copy the output `.asi` to the game directory. The output artifact is `Autosave.VC.asi`.

### San Andreas

```bash
# Release build (recommended for testing in-game)
.\build_sa.bat

# Debug build (includes .pdb symbols; debugger configured to launch gta_sa.exe)
.\build_sa_debug.bat
```

Both scripts auto-discover MSBuild via `vswhere`, kill `gta_sa.exe` if running, and copy the output `.asi` to the game directory. The output artifact is `Autosave.SA.asi`.

To build manually via Visual Studio: open `Autosave.sln`, select **Debug GTA3**/**Release GTA3** for GTA III, **Debug VC**/**Release VC** for Vice City, or **Debug SA**/**Release SA** for San Andreas (platforms are **GTA3**, **VC**, or **SA**, not the usual x86/x64).

If the Plugin SDK is missing, clone it:
```bash
git clone https://github.com/DK22Pac/plugin-sdk
```
Then set `PLUGIN_SDK_DIR` to that directory.

## CI / Releases

`.github/workflows/build.yml` runs on every push to master. It clones the Plugin SDK, builds GTA III, Vice City, and San Andreas Release configurations, and publishes a GitHub release tagged `v1.0.{N}` where N is the total commit count. Release assets include:
- `Autosave.III.asi` + `Autosave.III.ini` (GTA III)
- `Autosave.VC.asi` + `Autosave.VC.ini` (Vice City)
- `Autosave.SA.asi` + `Autosave.SA.ini` (San Andreas)

Release notes are auto-generated from commits by GitHub.

The post-build events in the vcxproj (which kill game executables and copy to game dirs) are guarded by `if defined GTA_III_DIR` / `if defined GTA_VC_DIR` / `if defined GTA_SA_DIR` and do nothing in CI.

## Architecture

Everything lives in a single file: `source/Main.cpp`. It is organized into three logical layers:

1. **`Config` namespace** — compile-time constants (cooldowns, ranges, save slots). Change behavior here first before touching logic.
2. **`Utils` namespace** — stateless helper functions. Includes game-state queries (`IsOnMission`, `IsCutsceneRunning`, `IsGameSafeToSave`) and blip/marker utilities. `IsMissionGiverSprite` hard-codes the list of GTA III mission giver radar sprites used to distinguish mission markers from other blips.
3. **`AutosaveMod` class** — singleton that owns all mutable state and hooks into three Plugin SDK events:
   - `OnGameInit` — loads config from `Autosave.III.ini`
   - `OnGameProcess` — per-frame logic: load detection, autosave triggering, mission retry state machine
   - `OnDrawHud` — renders the "Autosaved" notification, retry prompt, and debug overlay

### Key design details

- **Triple-game support**: Uses conditional compilation (`#ifdef GTA3` / `#ifdef GTAVC` / `#ifdef GTASA`) for game-specific code. Differences include mission giver radar sprites, radar trace counts, font styles, text encoding (wchar_t vs char), camera struct names, save/load APIs, and mission stats access.
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

Set `Debug = 1` in `Autosave.III.ini` (GTA III), `Autosave.VC.ini` (Vice City), or `Autosave.SA.ini` (San Andreas). This enables an on-screen overlay (top-left) showing live state: proximity to marker, load detection, missions-passed count, on-mission flag, and mission-failed-text visibility. No rebuild required — the ini is read at game init.
