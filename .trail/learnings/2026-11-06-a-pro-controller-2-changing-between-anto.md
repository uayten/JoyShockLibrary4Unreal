---
title: "A Pro Controller 2 changing between cable and Bluetooth is held for 10 seconds and recognised by its serial, not swapped like a DualShock 4"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 30, tokens: 127804 }
id: l-76e6cd09
scope: repo
status: active
src: agent
touches:
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShock.h
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShock_Switch2.cpp
  - Source/JoyShockLibrary4Unreal/Public/JoyShockBlueprintLibrary.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockBlueprintLibrary.cpp
  - Source/JoyShockLibrary4Unreal/Public/JoyShockLibrary4Unreal.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockSubsystem.cpp
  - Source/JoyShockLibrary4Unreal/Public/JoyShockSubsystem.h
  - Source/JoyShockLibrary4Unreal/Public/JoyShockInterface.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockInterface.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockPolling.cpp
---

## Decided
When a Pro Controller 2 drops, the interface parks it for 10 s. Its disconnect is not announced, and it keeps its Connection Id, input device and player slot. The same controller arriving on the other transport takes all of that over silently. The match is the info block at 0x013000 (bytes before the colours: serial and ids), which is read over both USB and BLE. The cable exposes no MAC.

## Tried and ruled out
Swapping the transport inside one JoyShock, as the DualShock 4 does. A Switch 2 cannot hold both links: the Bluetooth link drops ~13 ms after the USB opens, and after unplugging it only comes back on a button press.

## Watch out
Four failures, each found on hardware, each now handled:
1. The editor next to a Standalone took the single WinUSB command interface first. The game then had no calibration or identity for 2 s, so the character walked on the fixed-centre fallback and the controller was announced as new. Fixed: the editor skips the USB init while yielded.
2. A connect that arrives before its identity, while something is parked, waits up to 5 s.
3. A freed library handle went to a Joy-Con switched on during the wait, which ended the wait and inherited the player. Fixed: the parked handle is reserved.
4. A parked controller missing from Get Connected Controllers came back from the wide listing as an "XInput Controller". Fixed: it stays listed.

In the code: FParkedSwitch2, TryParkSwitch2, RetireParkedSwitch2, DeferredSwitch2Connects (JoyShockInterface.*). JoyShock::set_sw2_info_identity. FJSL4UDeviceHandedOverEvent re-keys the subsystem cache. D-011, D-012, R-048 (keeping Bluetooth while cabled).
