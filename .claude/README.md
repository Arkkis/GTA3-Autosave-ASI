# GTA3 Autosave Mod

## Project Overview

This is a GTA III mod that provides automatic save functionality. The mod saves the game automatically when the player approaches mission markers and after completing missions, with an optional retry feature after mission failures.

## Project Structure

```
Autosave/
├── source/
│   └── Main.cpp           # Main mod implementation
├── bin/                   # Build output (gitignored)
│   └── GTA3/
│       ├── Debug/        # Debug builds (Autosave.III.asi)
│       └── Release/      # Release builds (Autosave.III.asi)
├── obj/                   # Build intermediates (gitignored)
├── .vs/                   # Visual Studio cache (gitignored)
├── Autosave.sln           # Visual Studio solution
├── Autosave.vcxproj       # Visual Studio project file
├── Autosave.III.ini       # Mod configuration file
├── build.bat              # Release build script
└── build_debug.bat        # Debug build script
```

## Build System

- **Platform**: Windows with Visual Studio toolchain (v145)
- **Target**: GTA III (Plugin SDK)
- **Output**: `Autosave.III.asi` (ASI plugin format)
- **Configuration**: Requires `PLUGIN_SDK_DIR` environment variable
- **Game Path**: Debug builds use `GTA_III_DIR` environment variable for auto-deploy

### Building

```bash
# Release build (recommended)
./build.bat

# Debug build
./build_debug.bat
```

Build scripts automatically:
1. Locate MSBuild using vswhere
2. Clean and build the solution
3. Kill gta3.exe if running (debug/release)
4. Copy the .asi file to `$(GTA_III_DIR)\scripts` (if GTA_III_DIR is set)

## Code Architecture

### Main Components

**Class**: `AutosaveMod` (source/Main.cpp:227)
- Main mod singleton that handles all autosave logic
- Hooks into Plugin SDK events (init, process, draw)

**Key Features**:
1. **Autosave near mission markers**: Saves when player enters mission blip radius
2. **Autosave on mission complete**: Saves after successful mission completion
3. **Mission retry system**: Offers Y/N prompt to retry from last autosave
4. **Post-load rotation**: Rotates player/camera toward nearest mission marker after loading

### Configuration (Autosave.III.ini)

```ini
Debug = 0  # Enable debug overlay (0=off, 1=on)
```

### Important Constants (Config namespace, line 26-34)

```cpp
AUTOSAVE_COOLDOWN_MS = 15000              // 15 second cooldown between saves
MISSION_BLIP_DETECTION_RANGE = 10.0f      // Distance to trigger autosave
MISSION_BLIP_ROTATION_RANGE = 15.0f       // Distance for post-load rotation
POST_LOAD_GRACE_PERIOD_MS = 500           // Prevent immediate save after load
AUTOSAVE_DISPLAY_DURATION_MS = 3000       // "Autosaved" notification duration
MISSION_COMPLETE_SAVE_SLOT = 6            // Save slot for mission complete
MISSION_RETRY_SAVE_SLOT = 7               // Save slot for retry feature
```

## Coding Conventions

- **Namespace organization**: Utils for helper functions, Config for constants
- **Naming**: PascalCase for functions, camelCase for variables, m_ prefix for members
- **Comments**: Section headers with `// ========` separators
- **State tracking**: Boolean flags with `m_was*` prefix for previous frame state
- **Plugin SDK**: Uses GTA III Plugin SDK for game integration

## Common Tasks

### Adding a new feature
1. Add configuration constant to `Config` namespace if needed
2. Add state tracking variables to `AutosaveMod` class members
3. Implement logic in appropriate event handler (OnGameProcess, OnDrawHud)
4. Add utility functions to `Utils` namespace if reusable

### Modifying save behavior
- Edit `HandleAutosave()` for mission marker saves
- Edit `HandleMissionRetry()` for mission complete/retry saves
- Both use `PerformAutosave()` which preserves game time

### Debugging
- Set `Debug = 1` in Autosave.III.ini
- Debug info displays at top-left (30, 30)
- Shows: near marker, load state, missions passed, on mission, fail text visible

## Plugin SDK Integration

This mod requires the GTA III Plugin SDK:
- Location set via `PLUGIN_SDK_DIR` environment variable
- Links against `Plugin_III.lib` (release) or `Plugin_III_d.lib` (debug)
- Include paths: Plugin_III, game_III, shared

### Installing Plugin SDK

If the Plugin SDK is missing from context, clone it to the current project directory:

```bash
git clone https://github.com/DK22Pac/plugin-sdk
```

Then set the `PLUGIN_SDK_DIR` environment variable to point to the cloned directory, or use the local clone by updating the project's include/library paths.

## Important Notes

- **Time preservation**: `PerformAutosave()` saves/restores game clock (normally advances 6 hours)
- **Safety checks**: `IsGameSafeToSave()` prevents saves during cutscenes, missions, vehicle entry, etc.
- **Load detection**: Detects game loads by monitoring timer wraparound
- **Mission detection**: Uses `CTheScripts::OnAMissionFlag` and mission giver radar sprites
- **Fail detection**: Monitors big message text for "MISSION FAILED" or "M_FAIL"

## GTA III Specifics

**Mission Giver Sprites** (Utils::IsMissionGiverSprite, line 75):
- Asuka, Catalina, Don, 8-Ball, El Burro, Ice Cold, Joey, Kenji, Misty, Luigi, Ray, Salvatore, Tony

**Safe States for Saving**:
- Not in cutscene
- Not on mission
- On foot (not in/entering/exiting vehicle)
- Not dead/arrested

## Visual Studio Configuration

- Platform Toolset: v145
- Target Platform: Win32
- Character Set: MultiByte
- Configurations: Debug GTA3, Release GTA3
- Post-build: Kills gta3.exe and copies to game directory
