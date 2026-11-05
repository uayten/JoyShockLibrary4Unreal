---
title: "A new Select node written by NodeScribe names its inputs Option 0 and Option 1, not False and True"
author: Antônio
date: 2026-11-05
branch: batch/2026-10-29
cost: { minutes: 9, tokens: 104436 }
id: l-b553691c
scope: repo
status: active
src: agent
touches:
  - ROADMAP.md
  - docs/hardware.md
  - docs/switch2.md
  - Content/UI/WBP_ConnectedJoyShockInfo.uasset
  - Source/JoyShockLibrary4Unreal/Public/JoyShockBlueprintLibrary.h
  - Content/EnhancedInput/JoyShock-GyroscopeRotation.uasset
  - Content/EnhancedInput/JoyShockInputMappingContext.uasset
  - README.md
  - README.pt-BR.md
---

## Watch out
- When NodeScribe creates a Select whose index is a boolean, the node's pins are still Option 0 / Option 1 at the moment it wires them, so `False = ...` / `True = ...` fail with "The node has no pin". Reading an existing Select back shows False / True, which is what makes the wrong names look right. Write `Option 0` (false) and `Option 1` (true).
- The plugin's button bit list in a comment once stopped at SL/SR with wrong values (mic missing), and a demo row built from it misnamed SL and SR and missed C, GL and GR. The JSMASK_* defines are the authority for button bits; the comment now points at them.

## Decided
- NodeScribe cannot set a pin on a node that already exists, so the two unwired Select inputs were connected with BlueprintTools: find_nodes (by title or node_class) for the node refs, get_node_infos for pin index ids, then connect_pins.

In the code: Content/UI/WBP_ConnectedJoyShockInfo (event Update Button Readout, called first thing from Tick); button bits in Source/JoyShockLibrary4Unreal/Public/JoyShockTypes.h (JSMASK_*), the comment above GetSimpleStateForHandle in JoyShockBlueprintLibrary.h.
