---
title: "Which controller drives the editor's menus is decided by Slate focus, and the plugin leaves it that way"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 23, tokens: 144989 }
id: l-0c4ae74c
scope: repo
status: active
src: agent
touches:
  - ROADMAP.md
  - Source/JoyShockLibrary4Unreal/Public/JoyShockInterface.h
  - Source/JoyShockLibrary4Unreal/Private/JoyShockInterface.cpp
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockPolling.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockLibrary4Unreal.cpp
  - Source/JoyShockLibrary4Unreal/Private/JoyShockEnumeration.cpp
  - docs/troubleshooting.md
---

## Decided
The maintainer chose on 2026-11-06 to leave editor menu navigation exactly as Unreal does it, and only document it (docs/troubleshooting.md, "The controller also moves the editor's focus").

## Tried and ruled out
Making every controller navigate with the mouse. Slate picks its user from the platform user, and the platform user is the player number, which also drives the player lights. Disabling controller navigation in the editor outside PIE was also offered and declined; `Slate.EnableGamepadEditorNavigation false` stays a manual option.

## Watch out
"The player 1 controller does nothing in the editor but player 2's does" is not a plugin bug. Slate keeps one focus per user. User 0 shares its focus with mouse and keyboard, so after a click in the level viewport the player 1 controller shows no blue frame in the menus. Users 1+ keep whatever focus they were left with. Confirmed by the user: after a click on a panel, the player 1 controller moved the frame.

In the code: Slate maps input device -> platform user -> Slate user index 1:1 (ISlateInputManager::GetUserIndexForInputDevice, SlateApplication.h). Gamepad input reaches Slate even when the app is unfocused unless Slate.RequireFocusForGamepadInput is set (FSlateApplication::PollGameDeviceState).
