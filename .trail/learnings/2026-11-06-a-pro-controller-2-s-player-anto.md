---
title: "A Pro Controller 2's player light over the cable needs no retry-until-acknowledged, unlike in JoySense"
author: Antônio
date: 2026-11-06
branch: master
cost: { minutes: 13, tokens: 99000 }
id: l-08e33d25
scope: repo
status: active
src: agent
touches:
  - ROADMAP.md
  - Source/JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockPolling.cpp
---

## Decided
R-039 closed on 2026-11-06 with no code change. On the user's Pro Controller 2, the player light came on over the cable both when the controller arrived switched off and when it came from Bluetooth (same player kept). The polling thread's normal send, after the cable setup, is enough.

## Watch out
JoySense4Unity found that the first command after the last cable-setup command (03:0D) gets no reply and does nothing, and had to retry the light until acknowledged. The plugin counts the USB light as delivered as soon as the write succeeds, without checking the reply, and it still worked here because the light goes out from the polling loop, after other traffic, not at the tail of the setup list. If the light is ever moved into the setup sequence, that quirk comes back.
Also: a Pro Controller 2 on the cable with its charging light off after Unreal closed (2026-11-05) was most likely a full battery; on 2026-11-06 the charging light stayed on and only the player light stayed stuck (R-043).

In the code: send_pending_output_reports in JoyShockPolling.cpp sends the player light; set_sw2_player_lights in JoyShock_Switch2.cpp returns true on a successful WinUsb_WritePipe and only drains the reply.
