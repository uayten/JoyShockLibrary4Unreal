# Handoff

Updated: 2026-11-06

## State
- Branch `master`, nothing pushed. Editor binaries built from this commit (unity build; the non-unity include check passed for the gyro change).
- Approved on hardware and closed this session: R-039 (no code change), R-043 (Nintendo player lights off on exit), R-042 (`JSL4U Get Reports Per Second`), R-037 (gyro as time slices, D-014).
- R-049 in progress: `LV_JoyShockGyroAim`, a gyro-aim demo level with per-controller buttons, A to mark the crosshair, both sticks turning the camera. Approved except the button colours, untested. The pawn's stick pitch inversion is provisional, waiting on R-001.
- `LV_JoyShockDemo` was renamed `LV_JoyShockLocalMultiplayer` (duplicated and the old one deleted: Unreal refused to move the map; nothing referenced it). Not yet opened in Play after the rename.
- R-001 rewritten into the user's design: bytes per protocol, one table with a row per controller for each axis's sign and the quarter turn when held sideways.
- *Em teste* still holds only Renan's items (R-029, R-002, R-003, R-012, R-017); they reach him after a push.

## Next step
R-001, first step: in `LV_JoyShockGyroAim`, show what Unreal receives on Gamepad Left X/Y and Right X/Y before any modifier (`Get Input Analog Key State` on the player controller), so each controller measures itself against up = +1. Then the table, then drop the pawn's inversion.

## Notes for the next session
- Measured 2026-11-06 with the pawn's inversion: Pro Controller 2 left, Joy-Con L sideways and Joy-Con R sideways agree; the Pro Controller 2 right stick goes the other way. The user has no Xbox pad at hand for a reference.
- NodeScribe: a quoted literal on a Text pin does not come back in the reading (nor through `Conv_StringToText`); `Append` a string and convert with `(String -> Text)`. A custom event's parameters change only by rewriting the graph with `replace`.
- Blueprint compile errors from `BlueprintTools.compile_blueprint` show only in the log (`LogBlueprint: Error`); the tool returns null either way.
- Plugin content can't be created directly: `create_asset` in `/Game/BossRush/Testes/`, then `AssetTools.move` to `/JoyShockLibrary4Unreal/`.

## Read first
CLAUDE.md, then R-001 in ROADMAP.md. For the code: `PresentSticks` in `JoyShockInterface.cpp` and the stick blocks of the parsers in `InputHelpers.cpp` (trail ask). `docs/demo.md` > *Gyro aim level* for the level's Blueprints. Not needed: the gyro slicer (`GyroTimeSlicer.h`) unless motion is touched.
