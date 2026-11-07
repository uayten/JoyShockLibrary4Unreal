# Handoff

Updated: 2026-11-07

## State
- Branch `master`, nothing pushed (11 commits ahead of `origin/master`). Editor binaries built from this code.
- Approved on hardware this session: R-023 (names above a demo character grow upwards) and R-050 (Switch 2 Is Charging reads the charge-state byte; tested on the Pro Controller 2 in Standalone). R-024 dropped, R-025 found already done, R-034 closed as fact F-017.
- Switch 2 battery percent is still the straight line from 3.0 V to 4.2 V, so a full Pro Controller 2 (3.728 V) shows about 60% and Medium. R-027 is now research on reading the controller's own 0-9 level instead; a generic lithium curve was tried and dropped (see the learning on the Switch 2 battery voltage).
- In progress: nothing.

## Next step
R-040, the editor option to stop finding and connecting controllers. Before code, ask the user its two open questions: Editor Preferences or a toolbar button, and whether it applies at once or on reopening the editor. Then R-048 (Pro Controller 2 back to Bluetooth by itself), which needs the user's controller on the cable.

## Notes for the next session
- If the editor is closed when the session starts, `unreal-mcp` stays "failed" after opening the editor; only the user can reconnect it in the session's MCP server list.
- To see a Switch 2 report whole, a temporary dump in `parse_switch2_report` logging the packet every 2 s worked well (removed; see F-017's source). Never commit it.
- Prior art for the Switch 2 protocol: ndeadly's switch2_controller_research for field meanings, Switch2Connect for the BLE/USB mechanics (F-006). Check them before measuring a field from scratch.

## Read first
CLAUDE.md, then R-040 in ROADMAP.md, `docs/architecture.md` (where enumeration and the Bluetooth link live) and D-008 in DECISIONS.md (the editor already yields to a game). Not needed for R-040: the parsers, the stick table, the demo Blueprints.
