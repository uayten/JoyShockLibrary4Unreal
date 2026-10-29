---
title: NodeScribe cannot add a node inside an existing exec chain of the demo widgets
author: Antônio
date: 2026-10-29
branch: batch/2026-10-29
cost: { minutes: 38, tokens: 282665 }
id: l-3d6bb6fb
scope: repo
status: active
src: agent
touches:
  - Config/Input.ini
  - ROADMAP.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/Switch2Bluetooth.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/Switch2Bluetooth.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockEnumeration.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/InputHelpers.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShock.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockBlueprintLibrary.cpp
  - Source/JoyShockLibrary4Unreal/Public/JoyShockTypes.h
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShock_Switch2.cpp
---

## Watch out
- `write_graph` without `replace` only creates new chains; it cannot splice a node into an exec chain that already exists.
- `write_graph` with `replace` refuses on `/JoyShockLibrary4Unreal/UI/WBP_ConnectedJoyShockInfo:EventGraph`, because its reading loses two reconvergences (`Construct` and `OnClicked of HeaderButton` both run into the same `IsValid`).
- So a new row in that widget that has to be filled from an existing event (for example Battery Current inside `Update Sensor Readout`, R-031) needs either a hand edit in the editor or a NodeScribe change. New Designer rows are fine: `write_object` with a `designer` section adds them.

## Decided
- R-030's panel was written as a fresh chain (`OnInitialized` -> looping timer -> `Update Sensor Readout`) for exactly this reason: a new event avoids touching the existing Tick/Construct chains.
