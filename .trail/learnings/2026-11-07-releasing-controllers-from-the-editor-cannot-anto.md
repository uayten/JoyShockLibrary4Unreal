---
title: "Releasing controllers from the editor cannot drop a Joy-Con's Bluetooth link, so the lights are what show it was let go"
author: Antônio
date: 2026-11-07
branch: master
cost: { minutes: 20, tokens: 156908 }
id: l-a47eff02
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
The editor preference that switches controllers off (R-040, D-018) releases each controller through the ordinary disconnect path. A Switch 2 on Bluetooth really disconnects, because the editor opened that link. A Joy-Con or other Switch 1 controller on Bluetooth stays connected, because Windows owns the link, and that is what lets another program use it. To show it was let go, the user chose on 2026-11-07 to reset its player light to player 1, the way Windows shows a freshly connected Joy-Con. A closing editor does the same. Everything else has its lights turned off.

## Tried and ruled out
Leaving the lights as they were on release: the Joy-Con kept showing the editor's player, as if it were still owned. Turning every light off, as the editor used to do on close: it does not match what Windows shows, so released Joy-Cons looked different from freshly connected ones.

## Watch out
The release light write has to happen before the read is turned into a disconnect. The disconnect branch takes _connectedLock and keeps it into the teardown, and a hidapi write has no timeout. A write placed after the loop, as the shutdown path does it, would run under the lock.
For Switch 1 controllers on USB, which light Windows leaves is unknown, so they still go dark.

In the code:
- Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockPolling.cpp: reset_player_lights_on_exit (mask 0x01 for !is_switch2 && !is_usb) is called in pollIndividualLoop's bReleasedByEditor branch before res = -1, and after the loop only for cancel_thread.
- Tested on the user's Joy-Cons and Pro Controller 2 on 2026-11-07.
