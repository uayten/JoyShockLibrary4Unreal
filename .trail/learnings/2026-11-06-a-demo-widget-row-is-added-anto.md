---
title: "A demo widget row is added with NodeScribe, then compiled and saved through the editor toolset by name, without listing the toolsets"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 14, tokens: 84255 }
id: l-ebf1d27b
scope: repo
status: active
src: agent
touches:
  - Content/UI/WBP_ConnectedJoyShockInfo.uasset
  - ROADMAP.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/InputHelpers.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShock.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockBlueprintLibrary.cpp
  - Source/JoyShockLibrary4Unreal/Public/JoyShockBlueprintLibrary.h
  - docs/blueprint-nodes.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockPolling.cpp
  - docs/output.md
---

## Decided
R-042's "Reports / s" row in the demo's controller panel was added entirely from the agent: a designer write that copies an existing row's look, an `after` insertion into the readout chain, then a compile and a save. No editor clicks were needed.

## Watch out
Listing the MCP toolsets to find the compile and save tools costs several thousand tokens; call them by name instead. The save tool's argument is `asset_paths`, not `assets`.
A new row placed with `like` copies the neighbour's font, width and slot fill, so it matches the panel without restating any property.
The NodeScribe MCP refuses connections whenever the editor is closed (it was, after a build); wait for the editor log's startup line before calling it.

In the code:
- write_object on /JoyShockLibrary4Unreal/UI/WBP_ConnectedJoyShockInfo.WBP_ConnectedJoyShockInfo with `designer` / `VerticalBox_2 : VerticalBox` / `  BatteryRow : HorizontalBox` / `  ReportRateRow : HorizontalBox like BatteryRow` and its three children `like` BatteryText, BatterySpacer, BatteryAnswer.
- write_graph with `after TextBlock.SetText (Target = $BatteryAnswer, In Text = $append (String -> Text)):` on its EventGraph.
- call_tool editor_toolset.toolsets.blueprint.BlueprintTools compile_blueprint {"blueprint": "<object path>"}; editor_toolset.toolsets.asset.AssetTools save_assets {"asset_paths": ["<object path>"]}.
