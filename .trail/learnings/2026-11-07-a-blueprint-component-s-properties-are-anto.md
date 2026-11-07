---
title: "A Blueprint component's properties are read by filtering on the property name, not the component's name"
author: Antônio
date: 2026-11-07
branch: master
cost: { minutes: 5, tokens: 171284 }
id: l-029505cc
scope: repo
status: active
src: agent
touches:
  - Content/BP_JoyShockCharacter.uasset
  - Content/UI/WBP_ControllerName.uasset
  - ROADMAP.md
---

## Decided
To see a component's settings on a Blueprint sheet (for example the demo character's controller-name widget), filter read_object by the property's own words ("draw size", "pivot", "relative", "space"). The answer comes back grouped under each component that has it.

## Tried and ruled out
Filtering by the component's name ("Controller Name Widget") returns an empty sheet: the filter matches property names, not component names. Filtering by "Widget" only finds properties whose own name contains it (Widget Class).

## Watch out
A very short filter (a single letter) matches most of the ~1,100 properties on a Character sheet and exceeds the tool's output limit. Use a real word.
When an editor that was closed at session start is opened later, the session's unreal-mcp server stays "failed" even once port 8000 answers. Only the user can reconnect a project MCP server (in the session's MCP server list); the agent cannot.

In the code:
read_object("/JoyShockLibrary4Unreal/BP_JoyShockCharacter.BP_JoyShockCharacter", "pivot")
