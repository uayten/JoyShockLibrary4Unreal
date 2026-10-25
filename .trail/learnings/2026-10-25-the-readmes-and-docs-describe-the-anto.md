---
title: "The READMEs and docs/ describe the plugin as it is, not its history or the original JoyShockLibrary"
author: Antônio
date: 2026-10-25
branch: master
cost: { minutes: 1, tokens: 1163 }
id: l-1dc40545
scope: repo
status: active
src: agent
touches:
  - README.md
  - README.pt-BR.md
  - docs/blueprint-nodes.md
---

## Decided
- README.md / README.pt-BR.md do not call the plugin a fork of JibbSmart's JoyShockLibrary, because nothing depends on that repository; JibbSmart stays in Credits.
- docs/blueprint-nodes.md has no section about the removed Project Settings page or about how axes and types differ from the original library. The user found both confusing and unneeded.

## Watch out
- In README*.md and docs/, write what the plugin does; leave out notes about removed features and comparisons with the original library unless the user asks for them.
- A controller is described as working only when someone confirmed it on hardware; implemented-but-untested is said as such.
