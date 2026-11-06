---
title: NodeScribe cannot label a True or False exec output on a node that is not a Branch
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 4, tokens: 64437 }
id: l-a69203d9
scope: repo
status: active
src: agent
touches:
  - Content/BP_JoyShockInitializer.uasset
---

## Watch out
- `true:` and `false:` are reserved aliases for a Branch's `then`/`else` pins. On any other node whose exec outputs are literally named True and False (a C++ function with ExpandEnumAsExecs, such as the subsystem's Ensure Local Player for Controller), NodeScribe rejects the label with "`True:` is not an output of this node. Outputs: True, False". Its own reading writes that label, so a reading pasted back with `replace` loses the wire and leaves everything under it loose.
- `write_graph` reports this as an error but still applies the rest. Re-read the graph after any `replace` on a node like that.

## Decided
- On 2026-11-06 (R-046) the loose wire was relinked with `BlueprintTools.connect_pins`: `find_nodes` by title gives the node, `get_node_infos` gives pin index ids (True was output 0). Then `compile_blueprint` with warnings_as_errors, and `AssetTools.save_assets` takes `asset_paths` as package paths (`/JoyShockLibrary4Unreal/BP_JoyShockInitializer`), not refPath objects.
- To add nodes inside an existing branch, `after Branch (...):` with a `true:` label works and avoids `replace` entirely.

In the code: Content/BP_JoyShockInitializer.uasset, functions HandleControllerConnectedInfo and Handle Controller Disconnected.
