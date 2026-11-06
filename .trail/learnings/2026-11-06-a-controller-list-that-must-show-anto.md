---
title: "A controller list that must show a joined Joy-Con pair as one entry is rebuilt from the plugin's state on every change, not patched per event"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 13, tokens: 76850 }
id: l-10f1b769
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
  - Content/BP_JoyShockGyroAimPawn.uasset
  - Content/UI/WBP_JoyShockGyroAimOverlay.uasset
  - ROADMAP.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/InputHelpers.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockBlueprintLibrary.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockInterface.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockInterfaceInternal.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockPlayerAssignment.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockStickTable.cpp
---

## Decided
The gyro-aim overlay rebuilds its whole button column from JSL4U Get Connected Controllers on every connect, disconnect, join and separate. A joined pair is one button, owned by the half with the lower connection id, labelled with both ids. Approved on hardware 2026-11-06 (join, separate, aim with the pair).

## Tried and ruled out
Patching the buttons per event (remove the partner's button on join, recreate it on separate). The plugin raises separate and disconnect for the same half in either order, and at startup a pair is already joined with no join event replayed, so per-event bookkeeping has to cover every ordering; rebuilding from current state covers them all.

## Watch out
- Inside On Disconnected the leaving controller may still be listed, and its partner may still point at it: the rebuild takes the leaving id and skips it, and does not let a half hide behind a partner that is leaving.
- When the picked half is merged into its partner's button, the selection moves to the partner, or no button would be lit.

In the code:
- /JoyShockLibrary4Unreal/UI/WBP_JoyShockGyroAimOverlay EventGraph: event Rebuild Buttons (Excluding : Int64), called from Wait For Controller Changes (On Connected, On Disconnected with the leaving id) and Wait For Joy-Con Pairing Changes (On Joined, On Separated).
