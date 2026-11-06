---
title: "Building demo Blueprints from the agent: text literals, custom event parameters and compile errors each need a workaround"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 58, tokens: 342484 }
id: l-c19f9b20
scope: repo
status: active
src: agent
touches:
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/GyroTimeSlicer.h
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShock.h
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockPolling.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockBlueprintLibrary.cpp
  - Source/JoyShockLibrary4Unreal/Public/JoyShockBlueprintLibrary.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockInterface.cpp
  - docs/blueprint-nodes.md
  - docs/motion.md
  - docs/demo.md
  - HANDOFF.md
---

## Decided
The gyro-aim demo level (R-049) was built entirely through NodeScribe and the editor toolset: assets created in /Game/BossRush/Testes then moved into the plugin, graphs written and rewritten with replace.

## Watch out
- A quoted literal on a Text pin (`In Text = "..."`) does not come back in the reading, and neither did a `Conv_StringToText` with a literal; `Append (A = "...")` then `(String -> Text)` does.
- An existing custom event's parameters cannot be changed in place: adding one means rewriting the graph with `replace: true` (and every caller's graph too, since NodeScribe cannot set a pin on a node that exists).
- `BlueprintTools.compile_blueprint` returns null whether or not the Blueprint compiled; errors appear only in the log as `LogBlueprint: Error`. Grep the log after each compile.
- `AssetTools.move` refused to rename a map; duplicate under the new name and delete the old one after checking `get_referencers`.
- Deleting a designer widget or variable that the graph still uses triggers an auto-compile with errors; harmless if the graph is rewritten right after, but those errors stay in the log.

In the code:
- create_asset is NodeScribe's and only writes under /Game/; then editor_toolset.toolsets.asset.AssetTools move to /JoyShockLibrary4Unreal/...
- Materials: editor_toolset.toolsets.material.MaterialTools create_material, add_expression (/Script/Engine.MaterialExpressionConstant3Vector), write_object `Constant = "(R=1.0,G=0.0,B=0.0,A=1.0)"`, connect_to_output, recompile.
