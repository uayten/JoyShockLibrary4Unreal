# Handoff

Updated: 2026-11-06

## State
- Branch `master`, nothing pushed. Editor binaries are built from HEAD (unity and non-unity green).
- Approved on hardware this session and closed: R-006, R-008, R-010, R-011, R-015, R-016, R-044 (only the application in front answers the controllers, D-010), R-045 (a Pro Controller 2 moving between cable and Bluetooth stays the same controller, D-012), R-047 (with a game running, the editor writes nothing to the controllers and skips the Pro Controller 2 USB init, D-011).
- *Em teste* holds only Renan's items: R-029, R-002 (console step), R-003 (debugger steps), R-012, R-017. They reach him only after a push, which the user decides.
- Outside this repo: the BossRush `CLAUDE.md` gained Renan's Switch 2 console and is uncommitted there, next to unrelated user changes in `Content/BossRush/Testes/`. The BossRush pointer to this submodule is not updated either; both are the user's call.

## Next step
R-046: a controller that reconnects spawns on top of a character already in the demo. It is the demo's player setup Blueprint, not the plugin; read it with NodeScribe before proposing anything, and check whether R-019 and R-028 share the cause.

## Open questions
- R-048 (research): can the Pro Controller 2 keep its Bluetooth link while cabled, so it returns by itself like a DualShock 4?
- R-040, R-027, R-001 and Is Charging at full battery: unchanged.

## Notes for the next session
- NodeScribe (`unreal-mcp`) only connects if the editor is already open when the session starts. Without it, stop and say so rather than reading `.uasset` files.
- In the editor, which controller moves the menus is Slate focus: player 1 follows the mouse click, the others keep their own focus. That is Unreal's behaviour, kept on purpose.
- With a Standalone open, the controllers' lights show the game's players, not the editor's.
- Log timestamps are UTC (user's time + 3 h). The Standalone writes `BossRush_2.log`.

## Read first
The R-046 item in ROADMAP.md, then NodeScribe's `Docs/FORMAT.md`, then the demo's player setup Blueprint. Plugin source only if the demo turns out to need something from it.
