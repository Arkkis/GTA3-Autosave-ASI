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

`.github/workflows/build.yml` runs on every push to master. It clones the Plugin SDK, builds GTA III, Vice City, and San Andreas Release configurations, and publishes a GitHub release tagged `v1.0.{N}` where N is the total commit count.

The Plugin SDK clone is **pinned to a specific commit** in the workflow. Upstream master is not usable as-is, for two independent reasons:

- Commit `6e79583a` renamed `CRunningScript::m_bAwake` to `m_bSkipWakeTime` without updating the matching `VALIDATE_OFFSET`, so the SDK's own `Plugin_VC` target fails to compile.
- Newer revisions moved the game headers into `game_III/enums`, `game_III/meta` (and the VC/SA equivalents). `Autosave.vcxproj` does not list those directories, so `CEntity.h` fails on `#include <eEntityStatus.h>`.

Bumping the pin therefore means adding the `enums` and `meta` include directories to all six configurations in `Autosave.vcxproj` and confirming all three targets still build.

Note the workflow's `paths:` filter does not include `.github/**`, so a change to the workflow alone will not trigger it — use `gh workflow run "Build and Release"` to dispatch manually. Release assets include:
- `Autosave.III.asi` + `Autosave.III.ini` (GTA III)
- `Autosave.VC.asi` + `Autosave.VC.ini` (Vice City)
- `Autosave.SA.asi` + `Autosave.SA.ini` (San Andreas)

Release notes are auto-generated from commits by GitHub.

The post-build events in the vcxproj (which kill game executables and copy to game dirs) are guarded by `if defined GTA_III_DIR` / `if defined GTA_VC_DIR` / `if defined GTA_SA_DIR` and do nothing in CI.

## Architecture

Everything lives in a single file: `source/Main.cpp`. It is organized into three logical layers:

1. **`Config` namespace** — compile-time constants (cooldowns, ranges, save slots). Change behavior here first before touching logic.
2. **`Utils` namespace** — stateless helper functions. Includes game-state queries (`IsOnMission`, `IsCutsceneRunning`, `IsGameSafeToSave`) and blip/marker utilities. `IsMissionGiverSprite` hard-codes the list of GTA III mission giver radar sprites used to distinguish mission markers from other blips.
3. **`ControllerInput` namespace** — XInput gamepad polling for the mission retry prompt. Owns its own static state (edge detection, connection status). `XInputGetState` is resolved at runtime via `LoadLibraryA`/`GetProcAddress` (trying `xinput1_4` → `xinput1_3` → `xinput9_1_0`), so no extra `.lib` is linked and a missing DLL just means "no pad".
4. **`AutosaveMod` class** — singleton that owns all mutable state and hooks into three Plugin SDK events:
   - `OnGameInit` — loads config from `Autosave.III.ini`
   - `OnGameProcess` — per-frame logic: load detection, autosave triggering, mission retry state machine
   - `OnDrawHud` — renders the "Autosaved" notification, retry prompt, and debug overlay

### Key design details

- **Single game version per target**: `Autosave.vcxproj` defines `PLUGIN_SGV_10EN` (III/VC) and `PLUGIN_SGV_10US` (SA), so the Plugin SDK bakes 1.0 addresses in at compile time. This is not just a project choice — the prebuilt `Plugin_III.lib` / `Plugin_VC.lib` / `Plugin.lib` in the SDK are themselves built with only those defines, and for SA the SDK is 1.0 US-only at the source level (game classes are tagged `SUPPORTED_10US`; `Events::initGameEvent` and friends use a bare `AddressList<0x748CFB, H_CALL>` with no per-version dispatch). III and VC are better off — `Events` there uses `AddressListMulti` with 10EN/11EN/Steam entries — but the shipped libs still only carry 10EN. Widening support therefore means rebuilding the SDK libs, not just editing this project. `AutosaveMod`'s constructor calls `IsUsableGameVersion()` and bails out with an `Error()` box before registering any event handler, since installing hooks at 1.0 addresses on another exe would patch unrelated code.
- **San Andreas 1.0 EU**: accepted even though the SDK only claims 1.0 US. The two images are the same build with two displaced code regions -- identical below `0x741000`, `+0x50` through `0x7C0FFF`, `+0x40` for the rest of `.text` -- and every address the mod uses lives in the identical region. Two SDK behaviours had to be worked around, both of which fail *silently* off 1.0 US:
  - **Events never install.** `AddressList<Addr, H_CALL>` expands to RefList entries tagged `GAME_10US_COMPACT`/`GAME_10US_HOODLUM`, and `EventList::PatchAll` patches only where the tag equals `GetGameVersion()`. On EU nothing matched, so `event += handler` patched nothing and reported nothing. The SA target therefore patches `0x53E981` (game process) and `0x53E4FF` (draw HUD) itself via `injector::MakeCALL` and does not use `plugin::Events` at all. It also does not use `initGameEvent` (`0x748CFB` is in the `+0x50` region) -- `OnGameInit()` runs from the first processed frame instead.
  - **Version-dispatched addresses resolve to null.** `GLOBAL_ADDRESS_BY_VERSION(a,b,c,d,e,f)` is *not* compile-time; it expands to `plugin::by_version_dyn(...)`, which switches on `GetGameVersion()` at load. The SDK only populated the 1.0 US column, so on EU `CTheScripts::ScriptSpace`, `CTheScripts::OnAMissionFlag`, `TheCamera`, the `CClock` members and the `CGenericGameStorage` entry points are all 0. The `SAGame` namespace in `Main.cpp` binds those directly instead. Symbols the SDK defines as plain constants (`CWorld::Players`, `CTimer`, `CCutsceneMgr`, `CRadar`, `CStats`, `CMessages`, `CFont`, `FrontEndMenuManager`) were never affected.

  Before using any new SA symbol, check how the SDK defines it: if it goes through `*_ADDRESS_BY_VERSION`, it must be added to `SAGame` or it will be null on EU. `tools/check_addresses.py` and `tools/compare_exe.py` reproduce the analysis against a pair of exes; `tools/identify_exe.py` reports which build an exe is.
- **Failure reporting**: every entry point runs behind `RunGuarded()`, which catches faults, writes the faulting address and module to `Autosave.<GAME>.log` next to the game exe, and disables the mod for the session instead of crashing the game. The same log records the detected game version and whether hooks installed -- no log file at all means the `.asi` was never loaded. Resolve a logged offset by building with `<GenerateMapFile>true</GenerateMapFile>` on the Release link and looking the RVA up in the `.map`.
- **Triple-game support**: Uses conditional compilation (`#ifdef GTA3` / `#ifdef GTAVC` / `#ifdef GTASA`) for game-specific code. Differences include mission giver radar sprites, radar trace counts, font styles, text encoding (wchar_t vs char), camera struct names, save/load APIs, and mission stats access.
- **Two independent save slots**: slot 7 is the "approach" autosave (written when entering a mission marker radius, used as the retry restore point); slot 6 is the "mission complete" autosave. They have separate cooldown timers.
- **Time preservation**: GTA III advances the in-game clock by 6 hours on every save. `PerformAutosave()` snapshots and restores `CClock` state around the save call to prevent this.
- **Load detection**: There is no explicit "load" callback in the Plugin SDK. Loads are detected by monitoring `CTimer` for a wraparound/reset (`m_lastGameTime` vs current game time).
- **Mission failure detection**: Checked by scanning the big-message text buffer for the strings `"MISSION FAILED"` or `"M_FAIL"`, since there is no dedicated API for this.
- **Controller input**: The retry prompt is the mod's only interactive element. It is answered by keyboard `Y`/`N` (`KeyPressed`) or by gamepad via `ControllerInput`. `CPad` is deliberately **not** used: on stock III/VC executables a joypad's D-Pad is folded into the left stick by `CapturePad()` and the `CControllerState::DPad*` fields are only written by bound keyboard actions, so `CPad` never sees a real controller's D-Pad there. Defaults (D-Pad Right = yes, D-Pad Left = no) mirror SA's own `ConversationYesJustDown`/`ConversationNoJustDown`. Prompt wording auto-switches to button names once `ControllerInput::WasEverUsed()` trips.
- **Post-load rotation**: After a detected load, the mod rotates the player and camera toward the nearest mission blip (within `MISSION_BLIP_ROTATION_RANGE`). A 500 ms grace period (`POST_LOAD_GRACE_PERIOD_MS`) suppresses the autosave trigger immediately after rotation so the player isn't auto-saved the instant they load.

### Adding new behavior

1. Add any new constants to the `Config` namespace.
2. Add state variables as `m_`-prefixed members of `AutosaveMod`.
3. Wire logic into `OnGameProcess` (state changes) or `OnDrawHud` (rendering).
4. Extract reusable queries into `Utils`.

## Debugging

Set `Debug = 1` in `Autosave.III.ini` (GTA III), `Autosave.VC.ini` (Vice City), or `Autosave.SA.ini` (San Andreas). This enables an on-screen overlay (top-left) showing live state: proximity to marker, load detection, missions-passed count, on-mission flag, and mission-failed-text visibility. No rebuild required — the ini is read at game init.
