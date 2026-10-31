# Handoff

Updated: 2026-10-31

## State
- Branch `batch/2026-10-29`, checked out, nothing pushed. `master` (`5e2ac56`) holds only validated work, up to R-030 (battery and Switch 2 sensors in the demo panel, approved on hardware).
- The branch holds 13 roadmap items, one commit each, all compiled (unity and non-unity). R-004 (2026-10-30) and R-009 (2026-10-31) were approved on hardware; the other 11 are untested: R-002, R-003, R-005, R-006, R-007, R-008, R-010, R-011, R-012, R-029, R-032. Those 11 are in *Em teste* with their steps, along with the older R-015, R-016 and R-017. The editor binaries are already built from this branch.
- R-031 is also on the branch, approved on hardware on 2026-10-31 (Is Charging on the Pro Controller 2, cable and Bluetooth, and the HUD's On Charging Changed).
- R-005 has to be redone with Unity closed: the 2026-10-31 attempt failed with "is missing one of the Switch 2 characteristics". The user attributes it to Unity's port of this plugin being open on the same controller; the log cannot prove it, but the failure cleared without restarting the editor.
- The user has a Portuguese checklist of these tests, `TESTES-2026-10-29.md` in the plugin root, excluded locally through `.git/info/exclude`. It still lists R-009 as pending and R-031 as not ready, and its R-007 line lacks the exception now in the ROADMAP: init repeats when the cable goes in while still on Bluetooth.

## Next step
Collect the user's test results ("R-xxx passou" or a log). Each pass: delete the item from the ROADMAP and fix docs that changed (R-032 makes `docs/hardware.md` and `docs/switch2.md` wrong: they say the magnetometer is Bluetooth only). When everything on the branch is approved, merge it into `master` (fast-forward) and delete the branch. A failure is fixed on the branch, in the same item's spirit.
If the user would rather start new work while testing: the demo items R-018 to R-026, which need NodeScribe and the editor.

## Open questions
- R-027 (battery percentage from a charge curve): any lithium curve moves the readings a lot (3.75 V would drop from ~62% to ~20%), so it needs a reference reading (the console's battery indicator) or the user's call before coding.
- R-001 needs a Switch 1 Pro, a right Joy-Con, a Pro Controller 2 and a Sony pad tested together; Renan has the full set.
- Is Charging was measured only on a Pro Controller 2 at mid charge. Unmeasured: a full battery on the cable (it may flicker near the end of the charge), and Joy-Con 2, which shares the report (Renan).

## Notes for the next session
- NodeScribe's `after` inserts into an existing exec chain. It cannot set a pin on a node that already exists; `BlueprintTools.connect_pins` can (`find_nodes`, then `get_node_infos` for the pin ids).
- After restarting the editor, the `unreal-mcp` connection may need the user to reconnect it with `/mcp`; this time it came back by itself.
- Build from PowerShell (`& "...Build.bat" ...`); from Git Bash the quoted path fails.
- In the 2026-10-31 log, plugging the cable while on Bluetooth took about 12 s to reach calibration, against the 2 s R-004's test expects, with the command interface reported as owned by another process. Not investigated; possibly Unity again. Check it when R-007 is tested with Unity closed.
- Close Unity before any Switch 2 test.

## Read first
`TESTES-2026-10-29.md` (what the user is testing), then the ROADMAP's *Em teste* section. Source only when a test fails: `JoyShockPolling.cpp` (R-006/7/8/10), `Switch2Bluetooth.cpp` (R-002/3/5/11/12), `JoyShock_Switch2.cpp` (R-032).
