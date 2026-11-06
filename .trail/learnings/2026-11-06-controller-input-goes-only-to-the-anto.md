---
title: "Controller input goes only to the application in front, games included, because editor and Standalone share the controllers"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 12, tokens: 29555 }
id: l-1b603858
scope: repo
status: active
src: agent
touches:
  - DECISIONS.md
  - ROADMAP.md
  - Source/JoyShockLibrary4Unreal/Private/JoyShockInterface.cpp
  - Source/JoyShockLibrary4Unreal/Public/JoyShockInterface.h
  - docs/input.md
  - docs/troubleshooting.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockPolling.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockEnumeration.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockLibrary4Unreal.cpp
---

## Decided
Every process with the plugin hands controller input to Unreal only while it is the foreground application. That covers the editor, Standalone and packaged games. The maintainer chose it on 2026-11-06 ("só quem está na frente"). The process stays connected and keeps reading; held input is released when it loses the front.

## Tried and ruled out
Holding input back only in the editor and letting games keep Unreal's default (answer from the background). On hardware the Standalone on the second monitor moved while the editor was being used. Unreal's own default (`Slate.RequireFocusForGamepadInput` off) is what caused both directions.

## Watch out
Two game windows on one PC (a multi-process network session) now take input only while in front. Do not "fix" that by adding a background exception without asking. PIE needs no exception: it is the editor process.
A controller stuck flying the level viewport camera is Slate focus (one focus per player; a click moves only player 1's), not this rule.

In the code: FJoyShockInterface::ShouldDispatchInput / SendControllerEvents (JoyShockInterface.cpp), D-010, docs/input.md "Only the window in front".
