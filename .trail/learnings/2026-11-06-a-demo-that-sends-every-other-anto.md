---
title: "A demo that sends every other controller to another player must keep a joined Joy-Con pair's partner with the picked half, and NodeScribe chains a second async…"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 30, tokens: 98199 }
id: l-c5d559f2
scope: repo
status: active
src: agent
touches:
  - docs/demo.md
  - ROADMAP.md
---

## Decided
The gyro-aim level's overlay sends the picked controller to player 0 and every other one to player 1. A joined Joy-Con pair is keyed on its group primary, so assigning either half moves both: the loop put the picked half on 0 and then its partner on 1, carrying the pair to the player with no pawn. The partner now goes to player 0 too (its Joined To Connection Id equals the picked id), and Wait For Joy-Con Pairing Changes re-runs the assignment on join and separate. Approved on hardware 2026-11-06: the pair aimed and was measured.

## Watch out
- Any demo or game loop that assigns "this one to X, all others to Y" breaks joined pairs the same way; check JoinedToConnectionId.
- NodeScribe: a line written after an async node's labelled blocks (On Joined:, On Separated:) is NOT chained to that node; it reads back as a detached island with no event. Put the next node under the async node's `then:` label instead. Always read the graph back after writing two async nodes in one event.
- The stick readout found that JSL4U Get Left/Right Stick return the raw simple_state, without PresentSticks' sign flip or the sideways quarter turn; on a sideways Joy-Con they disagree with what Unreal receives. That belongs to R-001, not to the demo.

In the code:
- /JoyShockLibrary4Unreal/UI/WBP_JoyShockGyroAimOverlay EventGraph: event Select Controller loop (isPicked OR isPartnerOfPicked -> Select Int), event Construct: Wait For Joy-Con Pairing Changes, then: Wait For Controller Changes.
- UJoyShockLibrary::JSL4UGetLeftStick / JSL4UGetRightStick in JoyShockBlueprintLibrary.cpp read jc->simple_state directly.
