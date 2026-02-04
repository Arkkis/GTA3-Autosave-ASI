# GTA III Autosave Mod

Automatically saves your game when you approach a mission, after completing one, and lets you retry failed missions from that save.

## Features

- **Autosave on approach** — saves automatically when you walk up to a mission giver marker
- **Autosave on completion** — saves after a mission is completed successfully
- **Mission retry** — when a mission fails, a prompt appears letting you press **Y** to reload from the last approach autosave, or **N** to dismiss it
- **Post-load orientation** — after loading, the player and camera rotate to face the nearest mission marker

## Requirements

- **GTA III version 1.0** — the mod does not work on patched versions. Downgrading can be easily found via a Google search.
- **ASI Loader** — download the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) by ThirteenAG. From the release assets, grab `dinput8.dll` and place it in your GTA III installation root directory (the folder containing `gta3.exe`).

## Installation

1. Download the latest release from the [Releases](../../releases) page
2. Place `Autosave.III.asi` and `Autosave.III.ini` into the `scripts` folder inside your GTA III directory

That's it. Launch the game and the mod will be active.

## Configuration

Open `Autosave.III.ini` in the `scripts` folder to adjust settings:

| Option | Values | Description |
|--------|--------|-------------|
| `Debug` | `0` / `1` | Enables an on-screen debug overlay showing the mod's internal state |
