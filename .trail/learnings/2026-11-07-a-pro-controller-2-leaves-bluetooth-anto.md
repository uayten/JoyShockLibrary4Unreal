---
title: "A Pro Controller 2 leaves Bluetooth on the USB command that starts cable input, so cable input and staying on Bluetooth cannot both be had"
author: Antônio
date: 2026-11-07
branch: master
cost: { minutes: 31, tokens: 108844 }
id: l-6a4fec79
scope: repo
status: active
src: agent
touches:
  - DECISIONS.md
  - ROADMAP.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockLibrary.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockLibrary.h
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockPolling.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary4Unreal.Build.cs
  - Source/JoyShockLibrary4Unreal/Private/JoyShockEditorSettings.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockEnumeration.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockInterface.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockLibrary4Unreal.cpp
---

## Decided
On 2026-11-07 the maintainer chose that a Pro Controller 2 on the cable always sends its input over the cable, for the latency (D-019). The current behaviour stays: the full USB set-up runs, Bluetooth ends, and after unplugging a button press brings the controller back.

## Tried and ruled out
- Blaming the cable. Plugging it in, Windows enumerating it, opening the HID and WinUSB interfaces and the SPI reads all leave Bluetooth up. The link drops 10-25 ms after the controller acknowledges 03:0D ("Initialise USB").
- Leaving 03:0D out. Bluetooth survives the cable and the unplugging, but the cable carries no input. The user refused that trade.
- Sending this PC's Bluetooth address in 03:0D instead of FF..FF. Most significant byte first, it changes nothing. Least significant byte first (the host the controller advertises as paired to), both links stay up, but the controller still drops Bluetooth about 1.2 s after unplugging and switches off until a button is pressed. It also showed up as a second controller, because the plugin expects the Bluetooth link to go.

## Watch out
Typing a console command into the wrong box sends nothing. Check the Output Log echo (`<cvar> = "<value>"`) before reading a round as a result: one round here silently ran with the default.

In the code:
- init_switch2 in Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShock_Switch2.cpp, last entry of the command list: { 0x03, 0x0d, 8, { 0x01, 0x00, 0xff x6 } }.
- Measured with temporary console variables (stage cut-off, skip one command, host address), removed afterwards. Facts in DECISIONS.md F-018.
