# Handoff

Updated: 2026-10-25

## State
- The plugin builds and works as of `ffd39e7` (PR #7, the 0x3F recovery for Switch 1 controllers, merged).
- A code review on 2026-10-25 found seven problems, now R-006 to R-012; none is fixed. R-001 to R-005 came from porting the Switch 2 path to Unity.
- Trail is wired in (`.trail/`, session hook in `.claude/settings.json`); its code map is local and rebuilt with `trail build`.
- The code-review-graph MCP timed out in that session, so the `CLAUDE.md` instruction to use it first could not be followed.
- Branch `master`, everything committed, nothing pushed.

## Next step
R-013: write `README.pt-BR.md`, short, and move the long sections of the current `README.md` into English pages under `docs/`. Start by reading `README.md` whole and proposing the page split (R-013 lists the proposed topics) before writing.

## Notes for the next session
- `README.md` stays untouched until R-014 (D-001).
- `CLAUDE.md` points at README sections (*Source layout*, *Where to change what*, *Things worth knowing...*, *Blueprint nodes*, *Reacting to controllers*, *Asking for a test*); so do R-001 and R-004. When a section moves to `docs/`, those references move with it.
- Before writing any README, read `~/.claude/skills/project-docs/references/readme.md`.

## Read first
`README.md`, the R-013 and R-014 items, D-001 and D-002. The source code is not needed for this task.
