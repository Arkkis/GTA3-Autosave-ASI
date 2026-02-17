# GTA Autosave

## Project Overview

An autosave mod for GTA III, Vice City, and San Andreas. Saves the game automatically when the player approaches mission markers and after completing missions, with an optional retry feature after mission failures. A single codebase (`source/Main.cpp`) targets all three games via conditional compilation.

## Project Structure

```
Autosave/
├── source/
│   └── Main.cpp              # Single-file implementation for all three games
├── bin/                      # Build output (gitignored)
│   ├── GTA3/
│   │   ├── Debug/            # Autosave.III.asi
│   │   └── Release/
│   ├── VC/
│   │   ├── Debug/            # Autosave.VC.asi
│   │   └── Release/
│   └── SA/
│       ├── Debug/            # Autosave.SA.asi
│       └── Release/
├── Autosave.sln              # Visual Studio solution
├── Autosave.vcxproj          # Visual Studio project (6 configurations)
├── Autosave.III.ini          # GTA III config
├── Autosave.VC.ini           # Vice City config
├── Autosave.SA.ini           # San Andreas config
├── build.bat                 # GTA III release build
├── build_debug.bat           # GTA III debug build
├── build_vc.bat              # Vice City release build
├── build_vc_debug.bat        # Vice City debug build
├── build_sa.bat              # San Andreas release build
└── build_sa_debug.bat        # San Andreas debug build
```

## Build System

- **Platform**: Windows with Visual Studio toolchain (v145)
- **Targets**: GTA III, Vice City, San Andreas via Plugin SDK
- **Output**: `Autosave.III.asi`, `Autosave.VC.asi`, `Autosave.SA.asi`
- **Requires**: `PLUGIN_SDK_DIR` environment variable
- **Optional**: `GTA_III_DIR`, `GTA_VC_DIR`, `GTA_SA_DIR` for auto-deploy

### Visual Studio Configurations

| Configuration | Platform | Defines | Output | Links |
|---------------|----------|---------|--------|-------|
| Debug GTA3 | GTA3 | `GTA3` | `Autosave.III.asi` | `Plugin_III_d.lib` |
| Release GTA3 | GTA3 | `GTA3` | `Autosave.III.asi` | `Plugin_III.lib` |
| Debug VC | VC | `GTAVC` | `Autosave.VC.asi` | `Plugin_VC_d.lib` |
| Release VC | VC | `GTAVC` | `Autosave.VC.asi` | `Plugin_VC.lib` |
| Debug SA | SA | `GTASA` | `Autosave.SA.asi` | `Plugin_SA_d.lib` |
| Release SA | SA | `GTASA` | `Autosave.SA.asi` | `Plugin_SA.lib` |

Platforms are `GTA3`, `VC`, `SA` — not the usual x86/x64.

### Build Scripts

Each script auto-discovers MSBuild via `vswhere`, kills the game exe if running, and copies the output `.asi` to the game directory (if the corresponding env var is set).

## Code Architecture

All code lives in `source/Main.cpp`, organized into:

1. **`Config` namespace** — compile-time constants (cooldowns, ranges, save slots)
2. **`Utils` namespace** — stateless helpers for game-state queries and blip utilities
3. **`AutosaveMod` class** — singleton with all mutable state, hooks Plugin SDK events

### Conditional Compilation

Game-specific code is guarded by `#ifdef GTA3`, `#ifdef GTAVC`, `#ifdef GTASA`. Differences include:
- Mission giver radar sprites
- Radar trace counts and blip struct access
- Font styles and text encoding (`wchar_t` for GTA III, `char` for VC/SA)
- Camera struct names
- Save/load APIs
- Mission stats access

### Plugin SDK Integration

Each game links against its own Plugin SDK library:
- GTA III: `Plugin_III.lib` / `Plugin_III_d.lib`, includes `Plugin_III`, `game_III`, `shared`
- Vice City: `Plugin_VC.lib` / `Plugin_VC_d.lib`, includes `Plugin_VC`, `game_VC`, `shared`
- San Andreas: `Plugin_SA.lib` / `Plugin_SA_d.lib`, includes `Plugin_SA`, `game_SA`, `shared`

## Coding Conventions

- **Namespace organization**: `Utils` for helpers, `Config` for constants
- **Naming**: PascalCase for functions, camelCase for variables, `m_` prefix for members
- **Comments**: Section headers with `// ========` separators
- **State tracking**: Boolean flags with `m_was*` prefix for previous frame state
