# RE:CONFIG

`RE:CONFIG` is a minimal platform puzzle prototype where keyboard-operated settings change world rules and state. The core premise is solving puzzles by changing configuration-like rules such as gravity, collision, and other system settings, not by simply deleting a single feature.

This build is intentionally limited to Room 00 and Room 01. It is for checking whether a first-time player can learn movement, EXIT, BRICK objects, and world-rule settings without a full tutorial layer.

## Current Scope

- Two 16:9 fixed-screen onboarding rooms
- No camera movement
- Rounded abstract player
- Dark geometric solid platforms
- Room 00: player, flat floor, EXIT only
- Room 01: player, BRICK wall, EXIT only
- Purple frame exit
- Two-pane Korean settings menu appears over the room instead of replacing the screen
- Room 01 system settings: `중력`, `A 타입 충돌`
- Reset restores player position, settings, and room state

## Controls

- Move: Left / Right
- Jump: Up
- Down: Unused
- Settings open / close: X
- Move between category/settings panes: Left / Right
- Select category/setting: Up / Down
- Toggle setting: Z
- Restart: R
- Fullscreen: F11

## Build

Visual Studio 2022 C++ Build Tools:

```bat
.\build.bat
```

Run:

```bat
.\run.bat
```

Size limit target:

```text
1,474,560 bytes
```
