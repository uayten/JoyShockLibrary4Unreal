# Handoff

Updated: 2026-11-06

## State
- Branch `master`, nothing pushed. Editor binaries built from this code (unity; the non-unity include check passed for the stick table).
- Approved on hardware and closed this session: R-049 (gyro-aim level), R-021 and R-022 (done by the user). R-001 (stick table, D-015) is approved here and waits only on Renan's controllers; R-020 (slots reset at the end of a game, D-016) is answered but the reset is compiled only.
- R-023 in progress: in `LV_JoyShockLocalMultiplayer` each controller panel has **Share player 1** / **Own player**, approved; the names above a character refresh when those buttons are clicked.
- `LV_JoyShockGyroAim` now hands its one pawn to the picked controller's player (D-017); the demo's rumble test is on L (light) and R (strong).

## Next step
R-023, last item: the controller names above a character (`WBP_ControllerName`, one `WBP_ControllerNameText` per controller in `ControllerInfoVerticalBox`) should grow upwards, not downwards, as controllers are added. Read the widget's designer with `read_object` first; it sits in the character's `Controller Name Widget` component.

## Notes for the next session
- NodeScribe limits met today: a `True:`/`False:` label on bool-as-exec nodes (Ensure Local Player For Controller) is mapped to `then` and fails, link that pin with BlueprintTools `connect_pins`; engine events go by function name (`event K2_PostLogin`); `after X (...)` cannot tell apart two nodes that differ only in a default-valued pin; orphans after a failed write block `replace`, remove them with BlueprintTools `delete_node`. All in `.trail/learnings/`.
- BlueprintTools' compile returns null either way; errors show only as `LogBlueprint: Error` in `Saved/Logs/BossRush.log`.
- Unreal negates Gamepad Right Y for every gamepad (F-015): a game reading -1 there is correct.
- Joy-Con sticks read 0.7-0.9 at full travel in the stick check; not investigated (calibration, or the 0.25 AxisConfig dead zone the readout sees).

## Read first
CLAUDE.md, then R-023 in ROADMAP.md and `docs/demo.md` > *Local multiplayer level*. Not needed for R-023: the stick table, the parsers, the gyro slicer.
