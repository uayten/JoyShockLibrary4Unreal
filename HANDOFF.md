# Handoff

Updated: 2026-10-26

## State
- The plugin builds and works as of `ffd39e7`. Since then only comments and docs changed (not compiled; three header comments that become tooltips were edited).
- The Pro Controller 2 was tested on hardware for battery and the extra sensors (F-009), and a Bluetooth limit in Standalone was found (F-010). The docs, comments and `docs/hardware.md` now say what was measured.
- Roadmap: R-030 (demo sensor panel) is first in *To do*; R-029 and R-031 to R-034 came out of this session. None is in progress.
- Branch `master`, everything committed, nothing pushed.

## Next step
R-030: write the Designer rows and the graph into `/JoyShockLibrary4Unreal/UI/WBP_ConnectedJoyShockInfo` with NodeScribe, using the two texts in the item, then compile and ask the user to test with the Pro Controller 2. Ask before writing: it is a plugin asset.

## Open questions
- NodeScribe's Designer support (its R-001, commit `963fe31` in the NodeScribe repo) waits for the user's test. The user is working on NodeScribe before coming back here; if its texts changed meanwhile (Append with more pins, `Set Visibility`, split-struct names now fixed), R-030's texts can be simplified.

## Notes for the next session
- NodeScribe is reached through the `unreal-mcp` server; if it failed to connect, the user reconnects it with `/mcp`. Read only the *Widget Designer* section of `Plugins/NodeScribe/Docs/FORMAT.md` for R-030.
- BossRush has `Content/BossRush/Testes/BP_TesteSensoresPro2`, the stopgap sensor printer; delete it once R-030 is approved. Two unsaved drafts (`WBP_TesteSensores`, `WBP_TesteSensores2`) may also be there if they were saved by accident.
- A Switch 2 over Bluetooth only works in PIE or with the editor closed (F-010).

## Read first
`ROADMAP.md` item R-030 and the *Widget Designer* section of NodeScribe's `FORMAT.md`. The plugin source is not needed for R-030.
