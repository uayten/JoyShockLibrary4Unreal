---
title: "A game reads the right stick's vertical inverted because Unreal negates it for every gamepad, so the plugin must not compensate"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 24, tokens: 144845 }
id: l-9f20a117
scope: repo
status: active
src: agent
touches:
  - Content/UI/WBP_JoyShockGyroAimOverlay.uasset
  - ROADMAP.md
  - docs/demo.md
  - Source/JoyShockLibrary4Unreal/Private/JoyShockStickTable.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockStickTable.cpp
  - docs/architecture.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary4Unreal.Build.cs
---

## Decided
The plugin sends both sticks as up = +1, right = +1 (XInput's convention). Unreal's game viewport then negates Gamepad_RightY for every device, Xbox included, before PlayerInput and Enhanced Input see it. So a game reads -1 with the right stick up, and that is correct. Stick signs now live only in the stick table; the JSL4U stick getters return what the plugin sends (up = +1 on both) and pass through the same table, sideways Joy-Con turn included.

## Tried and ruled out
Treating the -1 on the right stick as a plugin regression and flipping it back (R-001's first diagnosis, 2026-11-06). Measured on Pro Controller 2, DualShock 4 and a Joy-Con pair, all three read right-up = -1 and left-up = +1: exactly the engine's flip. "Negated twice" in the old code was two flips that cancelled on purpose.

## Watch out
- Get Input Analog Key State reads after that negation and after the legacy AxisConfig dead zone (0.25), so Joy-Con values below 1 there are not necessarily the stick.
- A camera that looks up with both sticks needs a Negate on one of them, as with an Xbox pad. Never add a flip in the plugin for it.

In the code:
- Engine: FSceneViewport::OnAnalogValueChanged, Engine/Source/Runtime/Engine/Private/Slate/SceneViewport.cpp:1352 (UE 5.8): `Key == EKeys::Gamepad_RightY ? -Value : Value`.
- Plugin: Private/JoyShockStickTable.cpp (rows, PresentSticks), used by FJoyShockInterface::ProcessAnalogInputs and PresentedSticksForHandle in JoyShockBlueprintLibrary.cpp.
