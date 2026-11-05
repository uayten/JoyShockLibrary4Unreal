# Handoff

Updated: 2026-11-05

## State
- Branch `master`, nothing pushed; approved and still-untested work both live there. The editor binaries are built from it (unity and non-unity both green).
- Approved on hardware and closed: R-004, R-009, R-031, R-032, R-041 (Pressed Buttons row in the demo panel), R-005, R-007, R-033 (the editor hands its Bluetooth Switch 2 controllers to a running game, D-008). R-035 and R-038 (gyro dead zones) were dropped: the READMEs now say the plugin follows GyroWiki.
- Still in *Em teste*: the user's Pro Controller 2 items R-002, R-003 (only the SYNC-failure step left), R-010, R-011; R-008 (any controller); R-006 and R-015 (Joy-Cons); R-016 (DualShock 4); Renan's R-029, R-012 (Joy-Con 2 only) and R-017.
- The USB-init retry on an unanswered command (D-009) is in, but the controller answered every command in the retest, so that path has never run.
- The user's Portuguese checklist `TESTES-2026-10-29.md` (plugin root, excluded via `.git/info/exclude`) is stale: it predates everything approved since 2026-10-31.

## Next step
Collect the remaining test results, starting with the Pro Controller 2 ones (R-002, R-003, R-010, R-011). Each pass: delete the item, fix docs that changed. Renan's three need a push first, and the user decides when.

## Open questions
- R-040 (editor option to leave controllers alone): where it lives (Editor Preferences or a toolbar button) and whether it applies at once.
- R-027 (battery percentage curve) needs a reference reading or the user's call before coding.
- R-001 needs a Switch 1 Pro, a right Joy-Con, a Pro Controller 2 and a Sony pad together (Renan).
- Is Charging is unmeasured at full battery on the cable and on Joy-Con 2 (Renan).

## Notes for the next session
- The user's Unity port has its controller option switched on for good; do not tell them to close Unity before tests.
- Log timestamps are UTC: 18:39 in `Saved/Logs/BossRush.log` is 15:39 for the user. The Standalone writes `BossRush_2.log`.
- `quit` in `Saved/NodeScribe/command.txt` is refused while PIE runs; stop it first with `EditorToolset.EditorAppToolset.StopPIE`.
- Build from PowerShell; from Git Bash the quoted path fails.

## Read first
The ROADMAP's *Em teste* section. Source only when a test fails: `Switch2Bluetooth.cpp` (R-002/3/11/12), `JoyShockPolling.cpp` (R-006/8/10/16), `JoyShockEnumeration.cpp` (R-002).
