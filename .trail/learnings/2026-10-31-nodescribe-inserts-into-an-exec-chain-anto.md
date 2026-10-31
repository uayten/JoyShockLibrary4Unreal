---
title: "NodeScribe inserts into an exec chain with after, and only BlueprintTools rewires an existing pin"
author: Antônio
date: 2026-10-31
branch: batch/2026-10-29
cost: { minutes: 15, tokens: 158682 }
id: l-3ecad7df
scope: repo
status: active
replaces: l-3d6bb6fb
src: agent
touches:
  - ROADMAP.md
  - HANDOFF.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/InputHelpers.cpp
  - Source/JoyShockLibrary4Unreal/Public/JoyShockTypes.h
  - docs/controllers.md
  - DECISIONS.md
  - docs/hardware.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/Switch2Bluetooth.cpp
---

## Decided
- R-031's panel row and live controller refresh in `WBP_ConnectedJoyShockInfo` were written with NodeScribe's `after`, which replaced the old limit on splicing into a chain.
- The missing `New Controller` wire on the HUD's On Charging Changed was made with `BlueprintTools.connect_pins`: `find_nodes` gives the node, `get_node_infos` the pin ids. Save with `AssetTools.save_assets`.

## Watch out
- NodeScribe cannot set a pin on a node that already exists, and `replace` refuses on `WBP_JoyShockHUD`'s EventGraph reading while it holds unused data nodes.
- `WBP_ConnectedJoyShockInfo` holds a `Controller` struct copied at creation, so anything read from it goes stale unless refreshed; `Update Sensor Readout` now refreshes it from `JSL4U Get Controller Info`.
- Unity with this plugin's port open is a plausible cause of "is missing one of the Switch 2 characteristics" on the same Pro Controller 2. The user attributed it to that and it recovered without restarting the editor; the log cannot prove the cause. Close Unity before judging Bluetooth tests.
