# Handoff

Updated: 2026-10-25

## State
- The plugin builds and works as of `ffd39e7`; no code changed since.
- Documentation is done: `README.pt-BR.md` is the main README and `README.md` its English translation, both short. The detail lives in twelve English pages under `docs/`, plus `docs/getting-started.pt-BR.md` (the one exception to D-002). The pages were reorganised around tables. Per-controller claims were checked in the code, not on hardware.
- The roadmap has R-001 to R-012 (fixes from the Switch 2 port and the 2026-10-25 code review) and R-015 to R-028 (tests, demo work, new items). None is in progress.
- Branch `master`, everything committed, nothing pushed.

## Next step
Work on the roadmap with the user: review priorities and the order of *To do*, and decide which item comes first. Use the roadmap skill.

## Open questions
- Should the missing Joy-Con 2 entries in `Config/Input.ini` (F-008) become a roadmap item?
- `docs/hardware.md` lists magnetometer, mouse and temperature among the Pro Controller 2's sensors, but `docs/switch2.md` says those are Joy-Con 2 sensors. Which is right?
- The comment on **Wait For Battery Changes** (`JoyShockAsyncActions.h`) says the Pro Controller 2 reports battery as Unknown, but `docs/controllers.md` says the Switch 2 controllers fill Battery Percent. One of them is out of date.

## Notes for the next session
- The docs describe the plugin as it is now: no history, no comparisons with the original JoyShockLibrary (trail learning in `.trail/learnings/`).
- When `docs/getting-started.md` changes, `docs/getting-started.pt-BR.md` needs a roadmap item to catch up (D-002).

## Read first
`ROADMAP.md`, whole: the next task is the roadmap itself. The docs and the source are not needed for it.
