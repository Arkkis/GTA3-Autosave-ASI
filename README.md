# GTA Autosave

Automatically saves your game when you approach a mission, after completing one, and lets you retry failed missions from that save. Supports GTA III, Vice City, and San Andreas.

## Features

- **Autosave on approach** — saves automatically when you walk up to a mission giver marker
- **Autosave on completion** — saves after a mission is completed successfully
- **Mission retry** — when a mission fails, a prompt appears letting you press **Y** to reload from the last approach autosave, or **N** to dismiss it
- **Post-load orientation** — after loading, the player and camera rotate to face the nearest mission marker

## Requirements

- **Game version 1.0** — the mod requires the original unpatched exe for each game. Downgrade guides: [GTA III](https://www.google.com/search?q=gta3+downgrade), [Vice City](https://www.google.com/search?q=gta+vice+city+downgrade+1.0), [San Andreas](https://www.google.com/search?q=gta+san+andreas+downgrade+1.0)
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
