# yankeehook

An internal trainer DLL for **War of Rights** (CryEngine 5.4, DX11), built for **offline / single-player and private-server use**.
It is injected into the running game; there is no loader. Not for use on public servers.

## Use

1. Start War of Rights (offline / your own server).
2. Inject `yankeehook.dll` with any injector (for example Process Hacker: process menu, Miscellaneous, Inject DLL).
3. Press **Delete** to open the menu (changeable in Settings → General).
4. Unload from Settings → General → "Unload yankeehook" before injecting a new build. Injecting a second copy on top of the first will not hook.

It only matches the game build it was made against (hooks check the code bytes and refuse to install on a mismatch).

## Features

- **Visuals:** box ESP with team check and colours, chams, bullet tracers (self / team / enemy colours), FOV changer (pauses while aiming), crosshair, third person and freecam hotkeys.
- **Weapon:** one "gun mods" switch (no sway/shake, no bullet drop, fast ADS, instant reload, infinite ammo and clip, no suppression, no head bob) with sliders for damage, bullet speed, reload time, suppression and reload animation speed.
- **Volley:** fires your weapon N times along a line with delay, ripple or random order, aim lock, per-bullet muzzle smoke, and a hold-to-fire hipfire hotkey.
- **Aim:** aim assist with lead and drop for bots (line-of-sight check), magic bullet (fires an extra bullet that starts in front of the target).
- **Player:** god mode, no teamkill punishment.
- **Configs:** save and load profiles; `default` loads on start (Settings → Config). Files are in `%LOCALAPPDATA%\yankeehook\configs`.

## Status

Written by reverse engineering one game build. Parts were tested in game and parts were not:

| Area | State |
|---|---|
| Menu, ESP, entity list, FOV, tracers | tested |
| God mode, instant reload, infinite ammo, no sway (zoom block), volley fire | tested |
| Chams | partially tested, detection depends on the scene |
| Aim assist, magic bullet, third person / freecam, crosshair, config system, fast ADS, no teamkill punishment | **not tested in game** |
| No recoil | not working (removed) |

If something crashes, the crash log is `%LOCALAPPDATA%\yankeehook\crash.log`. Antivirus tools often flag injected DLLs; the source is here so you can inspect and build it yourself.

## Build

Visual Studio (MSVC, x64, C++20). Open `yankeehook.slnx` or build `yankeehook.vcxproj` with the Release | x64 configuration.

## Credits

Dear ImGui (MIT), Varela Round (SIL OFL 1.1), and the menu shell from the foxware ui kit.
