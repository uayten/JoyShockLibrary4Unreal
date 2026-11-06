---
title: Testing an editor close while a Switch 2 advertises means closing in the two seconds before it connects
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 2, tokens: 16091 }
id: l-e14c016e
scope: repo
status: active
src: agent
touches:
  - ROADMAP.md
  - docs/hardware.md
---

## Decided
The shutdown-while-advertising test starts with the controller off and the editor fully loaded; the tester holds SYNC and closes the editor as soon as the lights run.

## Tried and ruled out
Holding SYNC before opening the editor and closing it once loaded. On 2026-11-06 the Pro Controller 2 connected during the editor's load (frame 0) both times, so it was connected, not advertising, when the editor closed. The close was clean, but the discovery callback the shutdown fix guards was not running.

## Watch out
A clean exit in that run proves nothing about the advertising case. Before you call the test passed, check that each log has "is advertising" with no "Connected to Switch 2" after it before "LogExit: Preparing to exit". From advertising to connected took about 2.4 s.

In the code: the fix is commit a97c159 (Switch2Ble::Shutdown waits for a discovery callback in flight; ShutdownModule waits for the startup thread). Logs are in Saved/Logs/BossRush*.log, timestamps in UTC.
