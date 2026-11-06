---
title: "Plugging or pulling a cable never disconnects a controller, for every family including Switch 2"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 1, tokens: 11509 }
id: l-09319847
scope: repo
status: active
src: agent
touches:
  - ROADMAP.md
  - docs/troubleshooting.md
---

## Decided
The maintainer chose one rule for every controller family on 2026-11-06: moving between cable and Bluetooth keeps the controller connected, with the same Connection Id, player slot and Pawn. This is what consoles do when a pad is plugged in to charge. The DualShock 4 already behaves this way. The Pro Controller 2 did not: each swap reached the game as a disconnect plus a connect. Fixing that is roadmap item R-045.

## Watch out
A Switch 2 cannot keep both links open. Its Bluetooth link is gone once the cable is in, and after the cable is pulled it is a fresh BLE connection. So the swap has an input gap (measured 3.5 s to the cable, 4.6 s back to Bluetooth). The gap is accepted; reporting the swap as a disconnect is not. A controller that never comes back over Bluetooth (switched off, out of range) is still reported as disconnected, after a grace period (10 s proposed).
A game that wants to show "wired / wireless" needs a transport-changed signal, not a fake disconnect.

In the code: the DualShock 4 path is "switched to its USB connection; the Bluetooth path is kept as a fallback" in pollIndividualLoop (JoyShockPolling.cpp). The wired Switch 2 wins over its radio link in discover_bluetooth_switch2 (JoyShockEnumeration.cpp). docs/troubleshooting.md "Plugging in a cable while on Bluetooth" states the rule.
