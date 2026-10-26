---
title: "A Switch 2 Bluetooth connection is held by the editor, so Standalone from the editor never sees it"
author: Antônio
date: 2026-10-26
branch: master
cost: { minutes: 20, tokens: 211092 }
id: l-5d376f27
scope: repo
status: active
src: agent
touches:
  - docs/switch2.md
---

## Watch out
- Measured 2026-10-25 with the user's Pro Controller 2 over Bluetooth: the editor connected it (ble://..., init ran), Standalone started 6 s later and logged "0 controller(s) already connected" without ever attempting a connection. PIE, running inside the editor process, worked.
- Cause: the BLE/GATT session belongs to the process that opened it, and a connected controller stops advertising, so the second process has nothing to discover. Over USB the plugin hands the WinUSB interface over (idle hold released after 1 s, the loser retries); `Switch2Bluetooth.cpp` has no equivalent.
- So "the controller does not work in Standalone" with a Switch 2 on Bluetooth is not a demo or input-focus bug (compare F-004). Test it over the cable, in PIE, or with the editor closed.

## Decided
- Documented as a limitation in docs/switch2.md; a handover between editor and Standalone would be a roadmap item, not yet accepted.
