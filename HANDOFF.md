# Handoff

Updated: 2026-10-29

## State
- Branch `batch/2026-10-29`, checked out, nothing pushed. `master` (`5e2ac56`) holds only validated work, up to R-030 (battery and Switch 2 sensors in the demo panel, approved on hardware).
- The branch holds 13 roadmap items, one commit each, all compiled (unity and non-unity) and none tested on a controller: R-002, R-003, R-004, R-005, R-006, R-007, R-008, R-009, R-010, R-011, R-012, R-029, R-032. All are in *Testing* with their steps. The editor binaries are already built from this branch.
- R-031 is half done: `Battery Current` is read and published on Get Switch 2 Sensors, but not shown in the demo panel and not turned into Is Charging (see the item).
- The user has a Portuguese checklist of these tests, `TESTES-2026-10-29.md` in the plugin root, excluded locally through `.git/info/exclude`.

## Next step
Collect the user's test results ("R-xxx passou" or a log). Each pass: delete the item from the ROADMAP and fix docs that changed (R-032 makes `docs/hardware.md` and `docs/switch2.md` wrong: they say the magnetometer is Bluetooth only). When everything on the branch is approved, merge it into `master` (fast-forward) and delete the branch. A failure is fixed on the branch, in the same item's spirit.
If the user would rather start new work while testing: the demo items R-018 to R-026, which need NodeScribe and the editor.

## Open questions
- R-027 (battery percentage from a charge curve): any lithium curve moves the readings a lot (3.75 V would drop from ~62% to ~20%), so it needs a reference reading (the console's battery indicator) or the user's call before coding.
- R-001 needs a Switch 1 Pro, a right Joy-Con, a Pro Controller 2 and a Sony pad tested together; Renan has the full set.

## Notes for the next session
- NodeScribe cannot add a node inside an existing exec chain, and `replace` refuses on `WBP_ConnectedJoyShockInfo`'s EventGraph (trail learning of 2026-10-29). This blocks R-031's panel row; it is NodeScribe's R-015 now.
- After restarting the editor, the `unreal-mcp` connection may need the user to reconnect it with `/mcp`; this time it came back by itself.
- Build from PowerShell (`& "...Build.bat" ...`); from Git Bash the quoted path fails.
- `Content/BossRush/Testes/BP_TesteSensoresPro2` in BossRush can be deleted now that R-030 is approved; it was not, waiting for the user's ok.

## Read first
`TESTES-2026-10-29.md` (what the user is testing), then the ROADMAP's *Testing* section. Source only when a test fails: `JoyShockPolling.cpp` (R-004/6/7/8/10), `Switch2Bluetooth.cpp` (R-002/3/5/11/12), `JoyShock_Switch2.cpp` (R-009, R-032).
