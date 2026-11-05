---
title: "No gyro dead zone in the plugin: it follows GyroWiki, where good gyro games use calibration and sensitivity"
author: Antônio
date: 2026-11-05
branch: batch/2026-10-29
cost: { minutes: 3, tokens: 19335 }
id: l-6381be08
scope: repo
status: active
src: agent
touches:
  - README.md
  - README.pt-BR.md
---

## Decided
- The maintainer removed the gyro dead zone modifier (R-035, an Enhanced Input modifier that scaled readings below a degrees-per-second threshold) before it was tested, calling it a bad choice. The gate-style alternative (R-038) was dropped with it.
- The target is how Breath of the Wild and Splatoon feel, and those games use no gyro dead zone: calibration, a direct velocity-to-camera mapping with a sensitivity setting, and gyro mixed with the stick. The READMEs now say the plugin tries to follow GyroWiki's recommendations.
- The plugin reports the calibrated reading raw. Mixing with the stick, sensitivity and recentring belong to the game's camera code.

## Tried and ruled out
- Velocity dead zone with proportional scaling below a threshold: removed untested, because it eats the slow deliberate movement gyro aiming exists for.
- Gate dead zone that opens after a large turn: also dropped untested by the JoySense4Unity port the idea came from.

## Watch out
If jitter at rest ever needs handling, GyroWiki's answer is soft tiered smoothing (smooth only tiny velocities, pass larger ones raw), offered as an opt-in on the game side, never a threshold that zeroes or shrinks input.

In the code: the removed files were Source/JoyShockLibrary4Unreal/Public|Private/JoyShockInputModifiers.*, plus an EnhancedInput dependency in JoyShockLibrary4Unreal.uplugin and the Build.cs that existed only for them. The README paragraph sits right after "It fills the gaps" in README.md and README.pt-BR.md.
