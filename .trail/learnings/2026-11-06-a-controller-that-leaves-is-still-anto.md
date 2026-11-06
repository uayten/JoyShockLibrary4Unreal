---
title: "A controller that leaves is still read for a frame before the game hears, and a demo panel's timer kept reading it after leaving the screen"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 30, tokens: 206244 }
id: l-5f3b3f5d
scope: repo
status: active
src: agent
touches:
  - ROADMAP.md
  - Content/BP_JoyShockInitializer.uasset
  - Source/JoyShockLibrary4Unreal/Public/JoyShockInterface.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockInterface.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockBlueprintLibrary.cpp
  - Content/UI/WBP_ConnectedJoyShockInfo.uasset
---

## Decided
- The "asked to read connection N, which is not a connected controller" warning is skipped for 2 s after a connection ends. The controller leaves the interface in SendControllerEvents, but Wait For Any Controller Changes reports on a later core tick (on purpose, re-entrancy), so every per-frame reader lands in that gap once. A read still arriving after the grace is warned about as before.
- JSL4U Is Controller Connected never warns: asking whether a stored id is live is the correct thing to do with it.
- The warning now carries the Blueprint call stack (FFrame::GetScriptCallstack, empty in Shipping). That is what found the real reader below in one test round.

## Tried and ruled out
- Stopping WBP_ConnectedJoyShockInfo's looping 0.1 s timer from its Destruct event. The HUD rebuilds its panels with Clear Children and keeps the old ones in its Info Widgets by Connection Id map; Destruct did not fire for them within seconds, and the timer kept reading the gone id. The reader now checks JSL4U Is Controller Connected first instead.

## Watch out
- Warnings exactly 2 s after each disconnect mean something still reads the gone id; warnings in the same frame as the disconnect should no longer happen.

In the code: FJoyShockInterface::HasConnectionJustEnded / RecordEndedConnection (JoyShockInterface.cpp), HandleForConnection and JSL4UIsControllerConnected (JoyShockBlueprintLibrary.cpp), Update Sensor Readout in Content/UI/WBP_ConnectedJoyShockInfo.uasset.
