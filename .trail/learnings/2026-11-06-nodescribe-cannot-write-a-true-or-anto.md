---
title: "NodeScribe cannot write a True or False exec label on nodes whose pins are really named True and False, nor find an engine event by its displayed name"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 30, tokens: 183513 }
id: l-f23222ec
scope: repo
status: active
src: agent
touches:
  - Content/BP_JoyShockPlayerController.uasset
  - Content/EnhancedInput/JoyShock-LeftShoulder.uasset
  - Content/EnhancedInput/JoyShock-RightShoulder.uasset
  - Content/EnhancedInput/JoyShockInputMappingContext.uasset
  - ROADMAP.md
  - docs/demo.md
  - Content/UI/WBP_JoyShockGyroAimOverlay.uasset
  - docs/players.md
---

## Watch out
- On nodes whose exec outputs are named True/False (Ensure Local Player For Controller and other bool-as-exec functions), a `True:` label is mapped to the internal pin `then` (a Branch's name), so the write fails with "`True:` is not an output". The nodes under the label are still created, unlinked. Link the one pin with editor_toolset BlueprintTools.connect_pins (find the nodes with find_nodes, pin index from get_node_infos). The reader writes `True:`, so such a graph does not round-trip. Cause: ResolveLabelAlias in NodeScribeBuilder.cpp.
- An overridable engine event is matched by its function name, not its title: `event K2_PostLogin`, not `event OnPostLogin`, which silently became a Custom Event.
- After a failed write, `replace` refuses while orphan nodes remain; delete them with BlueprintTools.delete_node (find_nodes with an empty title lists every node; leave the greyed K2Node_Event defaults).
- AssetTools.move takes `path` and `new_path`.

In the code:
- Plugins/NodeScribe/Source/NodeScribeEditor/Private/NodeScribeBuilder.cpp: ResolveLabelAlias maps "true" -> "then".
