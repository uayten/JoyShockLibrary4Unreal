# Handoff

Updated: 2026-11-06

## State
- Branch `master`, nothing pushed. Editor binaries are built from HEAD (unity build; no function moved between files, so no non-unity check was needed).
- Approved on hardware this session and closed: R-046 (a reconnecting controller's mirror takes the first free slot), R-018, R-019 and R-028 (already fixed; re-tested in Standalone and closed).
- Also fixed and tested in Standalone, outside the roadmap: the unservable-id warning no longer fires in the frame a controller leaves, Is Controller Connected never warns, and the warning names the Blueprint that asked (D-013); the demo's sensor panel stops reading a gone controller; a Pro Controller 2 cabled before a Standalone starts keeps its identity, so its first trip off the cable is held (F-013).
- *Em teste* holds only Renan's items: R-029, R-002 (console step), R-003 (debugger steps), R-012, R-017. They reach him only after a push, which the user decides.
- BossRush: the pointer to this submodule and its `CLAUDE.md` (Renan's Switch 2 console) are committed there. The user's own content changes in BossRush (Zoo map, Tigre and sword animations, Testes assets) are left uncommitted for the user.

## Next step
No item is waiting here. The next move is the user's choice: publish (Renan's five items and this session's fixes reach him only through a push), or pick from *A fazer*. R-001 is first in order but needs a Switch 1 Pro Controller, which only Renan has; R-039 and R-043 are plugin work testable here.

## Open questions
- R-048 (research): can the Pro Controller 2 keep its Bluetooth link while cabled, so it returns by itself like a DualShock 4?
- R-040, R-027, R-001 and Is Charging at full battery: unchanged.

## Notes for the next session
- NodeScribe (`unreal-mcp`) needs the editor open; after closing and reopening it for a build, it reconnected by itself. NodeScribe cannot label a True/False exec output on a non-Branch node, nor rewire an existing pin: `BlueprintTools.connect_pins` does (trail learnings).
- In the editor, which controller moves the menus is Slate focus: player 1 follows the mouse click, the others keep their own focus. That is Unreal's behaviour, kept on purpose.
- With a Standalone open, the controllers' lights show the game's players, not the editor's.
- Log timestamps are UTC (user's time + 3 h). The Standalone writes `BossRush_2.log`.

## Read first
CLAUDE.md, then only the roadmap item the user picks. DECISIONS.md D-012, D-013 and F-013 if the work touches disconnects or the Pro Controller 2.
