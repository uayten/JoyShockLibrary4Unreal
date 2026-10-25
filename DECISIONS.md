# Decisions

<!-- project-docs: only what holds now. Next ID: D-006 · Next fact: F-008 -->

The invariants learned by breaking them (Bluetooth streams, `GetInstance`, the device mapper, the right
stick's Y, Connection Id, no CoreRedirects) live in `CLAUDE.md`, with their reasons. They are not repeated
here.

## Decisions

### D-001 The Portuguese README is written first; the English one follows it
Decision: `README.pt-BR.md` is the README that gets written and edited; `README.md` is its English translation, brought up to date afterwards.
Why: both maintainers are Brazilian and the user reviews in Portuguese. While the two differ, `README.md` stays as it was rather than becoming a stub.
Origin: user, 2026-10-25
Not chosen: English as the main README (agent's suggestion) -- the user reviews in Portuguese first.
Affects: R-013, R-014

### D-002 The docs/ pages are English only
Decision: the detailed pages under `docs/` exist in English only; both READMEs link to them.
Why: one copy of the long material to keep current; the READMEs are what a newcomer reads in their language.
Origin: user, 2026-10-25
Affects: R-013

### D-003 A controller keeps its player slot for as long as it is connected
Decision: a disconnect frees its slot as a hole, and a new controller takes the lowest free slot. Slots are not compacted.
Why: a game holds on to "player 1"; renumbering live players mid-match moves them under the game's feet. It is also what the demo architecture (D-004) stands on.
Origin: user, PR #6, 2026-07-19
Not chosen: compacting on disconnect (the last controller left becomes player 0) -- it renumbers players mid-match.

### D-004 The demo has one player controller per physical controller
Decision: on connect, `Create Player` with the plugin's player index spawns a `BP_JoyShockPlayerController`; on disconnect, `Remove Player`. Split screen is off. One model for split screen, party games and single player alike.
Why: one general model instead of one per kind of game.
Origin: agreed with the user, July 2026
Watch out: bind in the GameMode's BeginPlay, not the GameInstance's Event Init (subsystems do not exist yet); seed from `JSL4U Get Connected Controllers`, because controllers connected before the bind never fire the event; keep a map from Connection Id to player controller, because on disconnect the slot is already free and `JSL4U Get Player Index` answers -1; never `Remove Player` on player 0 (the primary local player) -- unpossess instead.
Affects: R-023, R-024

### D-005 Messages to Renan are written by Claude, in the first person
Decision: the user forwards them saying they are Claude's, so they are written as Claude and address him directly. His public pseudonym is Agent Ottsel; his real name is not confidential and is not to be raised again.
Why: the user said so on 2026-08-11.
Origin: user

## Verified facts

### F-001 Joy-Con 2 sensor ranges
Fact: Mouse Position is absolute (0 top-left, wraps at 2^16, integer); sliding a half the length of a Pro Controller moves X by ~12000. Mouse Distance ~3000 in the air, 140-150 resting on a surface. Mouse Roughness ~4600 in the air, ~4380 on a mousepad, ~2500 on a bare desk, ~2000 on cloth. Magnetometer X rises to ~6000 as a half nears the grip and settles at 1000-2000 once seated. Temperature is suspect: the L half read exactly the 25.0 constant (raw zero), the R half 36. Stick travel from centre: ~1200 counts on a Joy-Con 2, ~1600 on a Pro Controller 2.
Source: Renan's hardware measurements, 2026-08-04, build `e9f975b`
Verified: 2026-08-04

### F-002 Steam holds the Switch 2 command interface exclusively
Fact: with Steam running, opening the Pro Controller 2's WinUSB interface fails with ACCESS_DENIED. Input still streams if the controller was already initialised, but rumble and the calibrated init do nothing until Steam closes.
Source: user's hardware test, July 2026
Verified: July 2026

### F-003 The Pro Controller 2 has no audio interface over USB
Fact: it exposes only MI_00 (HID, input only) and MI_01 (WinUSB, commands). Headset audio through it is impossible.
Source: USB descriptors of the user's controller, July 2026
Verified: July 2026

### F-004 PIE drops the second local player's input; Standalone does not
Fact: the plugin dispatches the second controller's events to the right platform user. In PIE the second player's Slate user has focus on `SWindow` instead of `SPIEViewport`, and Slate drops the input before the viewport; in Standalone both players get `SViewport`.
Source: temporary ROUTING/DISPATCH logs in `JoyShockInterface.cpp`, 2026-07-22
Verified: 2026-07-22
Affects: R-019

### F-005 In Standalone, no controller exists yet at BeginPlay
Fact: enumeration runs on a background thread and finishes about a second after the game starts, so the controller list is empty at BeginPlay. Code that asks once at startup gets nothing; `UJoyShockSubsystem::ListenForControllers` binds and replays the controllers already connected.
Source: Standalone log, 2026-07-22
Verified: 2026-07-22
Affects: R-018, D-004

### F-006 Switch2Connect is the open reference for the Switch 2 protocol
Fact: `src/controller.py` has the whole BLE protocol (rumble frame encoding, commands, init, pairing, memory reads, input parser); `src/usb_hid_controller.py` the USB rumble routes and the 15 ms write rhythm; `src/discoverer.py` the advertisement filter. The rest (GUI, virtual drivers, ESP32 bridge) does not concern this plugin.
Source: https://github.com/TommyWabg/Switch2Connect
Verified: 2026-08-02

### F-007 Pro Controller 2 IMU layout
Fact: in report 0x05, accelX = +int16 @49, accelY = +@53, accelZ = -@51; gyroX (pitch) = +@55, gyroY (yaw) = +@59, gyroZ (roll) = -@57; bytes 61-62 are always zero. A wrong gyro mapping also makes gravity look axis-swapped during motion, so gravity is checked only after the gyro is right.
Source: user's hardware test against a Joy-Con in the same poses (flat and standing), July 2026
Verified: July 2026
