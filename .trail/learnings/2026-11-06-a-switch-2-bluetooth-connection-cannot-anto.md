---
title: A Switch 2 Bluetooth connection cannot be made to fail by hand; failing it takes a debugger pause
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 30, tokens: 167489 }
id: l-d6971e76
scope: repo
status: active
src: agent
touches:
  - ROADMAP.md
  - docs/hardware.md
  - Source/JoyShockLibrary4Unreal/Private/JoyShockEnumeration.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShock_Switch2.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/Switch2Bluetooth.cpp
---

## Decided
Tests of what happens when a Switch 2 Bluetooth connection fails part-way are written for someone with a debugger (Renan): pause inside the connect, switch the controller off, continue.

## Tried and ruled out
Switching the controller off by hand "as soon as the lights start running" after holding SYNC. On 2026-11-06, four attempts with the user's Pro Controller 2 all connected completely (colours, stick calibration, player light) about half a second after the plugin saw it, seconds before the controller went off. No human reaction lands inside that window.

## Watch out
A log that shows connect then disconnect is not a failed connection; it is an ordinary disconnect, which the R-010 test already covers. Read the log before calling a failure test passed.
A reconnect by button press shows "advertising ... paired to <PC address>" and no "paired ... to this PC" line; a SYNC press shows the "paired ... to this PC" line every time.

In the code: failure exits in Switch2Ble::Connect (Switch2Bluetooth.cpp), breakpoints at `Connection->Service = ServicesResult.Services().GetAt(0);` (~593) and `if (!Subscribe(Connection->InputCharacteristic))` (~733). Standalone log is Saved/Logs/BossRush_2.log, timestamps in UTC.
