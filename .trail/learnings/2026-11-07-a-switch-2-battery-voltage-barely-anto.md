---
title: "A Switch 2 battery voltage barely moves with charge, and the byte after it is a charge state, not a current"
author: Antônio
date: 2026-11-07
branch: master
cost: { minutes: 30, tokens: 140750 }
id: l-9c0d52d0
scope: repo
status: active
src: agent
touches:
  - DECISIONS.md
  - ROADMAP.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/InputHelpers.cpp
---

## Decided
Battery percent for Switch 2 controllers is to come from the controller's own 0-9 level (reports 0x07-0x09, per ndeadly's switch2_controller_research), not from a voltage curve. Check prior art (ndeadly's hid_reports.md and commands.md, SDL) before measuring a Switch 2 field from scratch.

## Tried and ruled out
A generic 4.2 V lithium discharge curve with 30 s smoothing and current compensation (2026-11-06). On the user's Pro Controller 2 the Bluetooth voltage was 3.713 V at more than half charge and 3.728 V full, so the curve read 19% on a nearly full controller. Dropped uncommitted.
Asking feature bit 0x20 over Bluetooth to get the battery current: the Pro Controller 2 still reports none (per ndeadly only the Joy-Con 2 report it, and 0x20 is the rumble feature).

## Watch out
Byte 0x21 of report 0x05 (packet[34] counting the report id) is a charge state: 0x34 while charging, 0x20 when full, 0 on battery. The plugin reads packet[34-35] as a current in hundredths of an amp, so the "0.52 A" in older notes is 0x34. On the cable the voltage stays near 3.73-3.75 V whatever the charge.
The temperature at 0x2E is documented by ndeadly as int16, 25 C + raw / 126.9, but measured raw values rose 3 to 8 in clean single steps over 7 minutes of charging, which does not fit that resolution. Unresolved (R-034).

In the code:
Source/JoyShockLibrary4Unreal/JoyShockLibrary/InputHelpers.cpp, parse_switch2_report (battery block and temperature). Roadmap R-027, R-034, R-050; DECISIONS F-009.
