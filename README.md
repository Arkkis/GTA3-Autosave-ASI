# GTA Autosave

Automatically saves your game when you approach a mission, after completing one, and lets you retry failed missions from that save. Supports GTA III, Vice City, and San Andreas.

## Features

- **Autosave on approach** — saves automatically when you walk up to a mission giver marker
- **Autosave on completion** — saves after a mission is completed successfully
- **Mission retry** — when a mission fails, a prompt appears letting you press **Y** to reload from the last approach autosave, or **N** to dismiss it
- **Controller support** — the retry prompt can also be answered on a gamepad: **D-Pad Right** for yes, **D-Pad Left** for no. The prompt switches to controller wording once you've used a pad, and the buttons are rebindable in the INI. Requires an XInput-compatible controller; everything else in the mod is automatic and needs no input at all
- **Post-load orientation** — after loading, the player and camera rotate to face the nearest mission marker

## Requirements

- **Game version 1.0** — the mod requires the original unpatched exe for each game. Nothing else works: 1.1 / 1.01, the Steam and Rockstar Launcher builds, and the Definitive Edition are all unsupported, and for San Andreas the **US** 1.0 exe is required (1.0 EU is a different build and will not work). If you launch the game with an unsupported exe the mod shows an error box naming the version it found, then disables itself.

  | Game | Supported exe |
  |------|---------------|
  | GTA III | 1.0 EN |
  | Vice City | 1.0 EN |
  | San Andreas | 1.0 US (`Compact` or `HoodLum`) |

  Downgrade guides: [GTA III](https://www.google.com/search?q=gta3+downgrade), [Vice City](https://www.google.com/search?q=gta+vice+city+downgrade+1.0), [San Andreas](https://www.google.com/search?q=gta+san+andreas+downgrade+1.0+us)
- **ASI Loader** — download the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) by ThirteenAG. From the release assets, grab `dinput8.dll` and place it in your game's installation root directory (the folder containing the game exe).

## Installation

1. Download the latest release from the [Releases](../../releases) page
2. Place the files for your game into the `scripts` folder inside the game directory:
   - **GTA III** — `Autosave.III.asi` + `Autosave.III.ini`
   - **Vice City** — `Autosave.VC.asi` + `Autosave.VC.ini`
   - **San Andreas** — `Autosave.SA.asi` + `Autosave.SA.ini`

That's it. Launch the game and the mod will be active.

## Configuration

Open the INI file for your game (`Autosave.III.ini`, `Autosave.VC.ini`, or `Autosave.SA.ini`) in the `scripts` folder to adjust settings:

| Option | Values | Description |
|--------|--------|-------------|
| `Debug` | `0` / `1` | Enables an on-screen debug overlay showing the mod's internal state |
| `ApproachAutosave` | `0` / `1` | Autosave when approaching a mission marker (default: enabled) |
| `MissionCompleteAutosave` | `0` / `1` | Autosave after completing a mission (default: enabled) |
| `ControllerRetryYes` | button name | Controller button that reloads the autosave at the retry prompt (default: `DPadRight`) |
| `ControllerRetryNo` | button name | Controller button that dismisses the retry prompt (default: `DPadLeft`) |

Accepted button names: `None`, `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`, `Start`, `Back`, `LeftThumb`, `RightThumb`, `LeftShoulder`, `RightShoulder`, `A`, `B`, `X`, `Y`. Use `None` to disable that binding. The keyboard **Y** / **N** keys always work regardless.
