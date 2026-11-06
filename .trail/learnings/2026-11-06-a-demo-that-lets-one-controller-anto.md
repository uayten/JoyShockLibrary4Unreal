---
title: "A demo that lets one controller drive a pawn hands the pawn to that controller's player instead of moving controllers between players"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 30, tokens: 183513 }
id: l-670cae0f
scope: repo
status: active
src: agent
touches:
  - Content/BP_JoyShockPlayerController.uasset
  - Content/EnhancedInput/JoyShock-LeftShoulder.uasset
  - Content/EnhancedInput/JoyShock-RightShoulder.uasset
  - Content/EnhancedInput/JoyShockInputMappingContext.uasset
  - ROADMAP.md
  - docs/demo.md
  - Content/UI/WBP_JoyShockGyroAimOverlay.uasset
  - docs/players.md
---

## Decided
The gyro-aim level used to put the picked controller on player 0 and every other controller on player 1. The maintainer asked (2026-11-06) for controllers to keep their own player and light, and for the level's pawn to change hands instead: Ensure Local Player For Controller returns the picked controller's Player Controller, creating the player if needed, and it Possesses the one pawn. Approved on hardware the same day.

## Tried and ruled out
Moving controllers between players with JSL4U Assign Controller To Player Index. It changed player lights, broke joined pairs unless the partner was moved too, and carried into the next Play In Editor session, since slots live in the module.

## Watch out
- With players created on demand, the Game Mode must not spawn a pawn per player: Default Pawn Class is None and K2_PostLogin spawns the pawn for player 0 only.
- Split screen must be off (Set Force Disable Splitscreen) so player 0's view is the one drawn; the pawn points player 0's view target at itself on every possession.
- A player created in PIE needs Set Focus to Game Viewport one tick after possession, as BP_JoyShockCharacter does.

In the code:
- /JoyShockLibrary4Unreal/BP_JoyShockGyroAimGameMode EventGraph (K2_PostLogin), BP_JoyShockGyroAimPawn (event Possessed), UI/WBP_JoyShockGyroAimOverlay (event Select Controller).
