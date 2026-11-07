# Decisions

<!-- project-docs: only what holds now. Next ID: D-018 · Next fact: F-017 -->

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
Exception: `docs/getting-started.pt-BR.md`, a Portuguese translation of `docs/getting-started.md`, which README.pt-BR.md links to instead (user, 2026-10-25). When the English page changes, the Portuguese one gets a roadmap item to catch up.
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
Affects: the demo's local multiplayer level (`LV_JoyShockLocalMultiplayer`)

### D-005 Messages to Renan are written by Claude, in the first person
Decision: the user forwards them saying they are Claude's, so they are written as Claude and address him directly. His public pseudonym is Agent Ottsel; his real name is not confidential and is not to be raised again.
Why: the user said so on 2026-08-11.
Origin: user

### D-006 A report-mode recovery is paced, and never repeated freely on Bluetooth
Decision: a recovery that re-sends configuration because a controller streams the wrong report runs a bounded number of times per episode (wrong report until the first right one), with a gap between tries -- `FModeRecoveryPacer` in `JoyShockPolling.cpp`. Switch 2 on USB streaming anything but 0x05: the USB set-up again every 0.75 s, ten times, the first only after 0.75 s. Switch 1 in 0x3F over Bluetooth: three tries 2 s apart, the first at once. The Switch 2 init retry is USB only (the WinUSB lease); a failed Bluetooth init is not retried, and a USB init that held the interface and still failed gets three retries.
Why: configuration on a live Bluetooth stream has dropped working controllers four times (CLAUDE.md); the numbers come from the Unity port's measurements (0.75 s) and are the agent's choice otherwise.
Origin: agent's choice, 2026-10-29, delivered for test with R-004, R-006 and R-007
Affects: R-004, R-006, R-007

### D-007 The Joy-Con 2 halves take the Pro Controller 2's feature mask
Decision: `JoyCon2Left` and `JoyCon2Right` are registered in `Config/Input.ini` with `SupportedFeaturesMask=9477` (Keypress, Gamepad, ForceFeedback, Acceleration, Orientation), like `Switch2ProController`, not 9541 like the Switch 1 Joy-Cons (which adds Lights).
Why: same family, same output path; whether player LEDs count as Lights is left to Renan's test.
Origin: agent's choice, 2026-10-29
Affects: R-029

### D-008 The editor yields its Bluetooth Switch 2 controllers to any game with the plugin
Decision: a game process (not the editor, not a commandlet) holds the named mutex `Local\JoyShockLibrary4Unreal.GameWantsBluetooth` while it runs; the editor checks for it every 0.5 s, and while it exists releases every Switch 2 it holds on Bluetooth (each polling thread reads it as a disconnect) and does not scan.
Why: a Bluetooth link belongs to the process that opened it and a connected controller stops advertising, so a Standalone game could never get a controller the editor held (F-010). A named object needs nothing from the editor modules in a runtime module, and also covers a packaged build started beside the editor.
Not chosen: the editor's Standalone-launch delegate (editor-only dependency, misses games started any other way); only a manual editor option (R-040), which still stays on the roadmap for using the controller in another program.
Origin: agent's choice, accepted by the user as "a automática", 2026-11-05
Affects: R-033, R-040, docs/switch2.md

### D-009 A Switch 2 USB init with an unanswered command counts as failed
Decision: in `init_switch2`, a command that gets no acknowledgement makes the init fail, so the poll loop's bounded USB retry (three tries, two seconds apart) runs it again.
Why: every healthy USB init in the logs had all ten commands acknowledged; the one with five unanswered (cable plugged while the Bluetooth link was opening) left the controller with no motion and no player light. Cable only: Bluetooth init is never re-run (CLAUDE.md).
Origin: agent's choice, 2026-11-05
Affects: R-007

### D-010 Only the application in front hands controller input to Unreal
Decision: `SendControllerEvents` dispatches input only while `FPlatformApplicationMisc::IsThisApplicationForeground()`; otherwise it keeps reading every controller but dispatches nothing, releasing held input on the frame it stops. Editor and games alike, packaged ones included; PIE needs no exception, being the editor process.
Why: Unreal dispatches gamepad input regardless of focus (`Slate.RequireFocusForGamepadInput` is off), and the editor and a Standalone it launched share the controllers: the controller walked Slate's navigation frame across the editor behind the game, and moved the game on the other monitor while the editor was in front (2026-11-06). Staying connected keeps the player slot for the return.
Not chosen: holding back only the editor (the first version, which the hardware test turned down); Unreal's default for games, which lets a stream or second-monitor game be played from behind another window, and multi-process network sessions on one PC take input in both windows.
Origin: agent's proposal, the user's call ("só quem está na frente"), 2026-11-06
Affects: R-044, docs/input.md

### D-011 With a game running beside it, the editor writes nothing to any controller
Decision: while the D-008 flag says a game with the plugin is running, the editor's polling threads send no player lights, light bar or rumble, and it skips the DualShock 4's connect-time light. When the game closes, each thread sends the editor's player light and rumble once more; the HOME light clear is not repeated. It also skips the Pro Controller 2's USB init, which takes the WinUSB command interface only one process can hold: the editor won that race by milliseconds on every plug, leaving the game without stick calibration (the character walked on the fixed-centre fallback) or identity for 2 s. The polling thread's retry runs the init once the game closes.
Why: both processes read the same HID controllers and each keeps its own player list, so both wrote player lights: a Joy-Con pair joined in both at once showed two different players, whichever write landed last (2026-11-06). The game is what is being played.
Not chosen: writing only from the process whose window is in front (a game answers from the background, D-010, and would lose its lights whenever the editor is clicked).
Origin: agent's choice, 2026-11-06
Affects: R-047, D-008

### D-012 A Pro Controller 2 moving between cable and Bluetooth is held, then carried on
Decision: when a Pro Controller 2 whose info-block identity is known disconnects, the interface parks it for 10 s instead of announcing it: input released, Connection Id, input device (still mapped and registered) and player slot kept. If a Pro Controller 2 with the same identity connects meanwhile, it takes all of that over under its new handle and nothing is announced; `OnDeviceHandedOver` tells the subsystem to re-key its cache. Otherwise the disconnect goes out when the time runs out. The identity is the hex of the bytes before the colours in block 0x013000 (serial and ids), read on both transports. A Pro Controller 2 that connects while another is parked, before its identity is read, waits unannounced for up to 5 s: over the cable the read waits for the USB command interface, which the editor beside a Standalone held for 2 s on 2026-11-06, and announcing it first made it a new controller for good. The parked controller's library handle is held under a key of its own meanwhile; freed, it went to a Joy-Con switched on during the wait, which ended the wait early and inherited the player. While parked it stays in Get Connected Controllers and Get Controller Info, described as it was at its connect: dropped from the list, its still-mapped input device came back from Get All Connected Controllers as an undriven "XInput Controller", and the demo gave it a second mirror.
Why: the user's rule is that a cable never disconnects a controller, for every family (2026-11-06). A Switch 2 cannot hold both links like a DualShock 4 -- the cable ends Bluetooth, and Bluetooth back is a fresh connection -- and the cable exposes no MAC to match on. Measured gaps: 3.5 s to the cable, 4.6 s back.
Not chosen: a transport swap inside one JoyShock, as the DualShock 4 does (the two transports are different objects, a BLE link and a HID+WinUSB pair, each with its own init); not parked when the editor yields its Bluetooth controllers to a game (D-008), which is a real departure.
Origin: agent's choice, 2026-11-06
Affects: R-045, F-013

### D-013 The unservable-id warning is for a reader that keeps going, not for the frame a controller leaves in
Decision: `HandleForConnection` does not warn for 2 s after a connection ends (`HasConnectionJustEnded`); a read after that is warned about as before, once per id. `JSL4U Is Controller Connected` never warns. The warning carries the Blueprint call stack (`FFrame::GetScriptCallstack`; empty in Shipping).
Why: a controller leaves the interface in `SendControllerEvents`, but Wait For Any Controller Changes reports it on a later core tick (on purpose, for the re-entrancy invariant), so every per-frame reader asks for it once after it is gone -- a warning there told a correct game to hunt for an unset variable. Asking whether a stored id is live is exactly what a game should do with it. Without the call stack the warning named no reader; with it, the demo's real one was found in one test.
Origin: agent's choice, tested by the user in Standalone, 2026-11-06
Not chosen: stopping the demo panel's reader from its Destruct event -- `WBP_ConnectedJoyShockInfo`'s looping timer kept reading after the HUD removed the panel with Clear Children, and Destruct did not stop it; the reader now checks Is Controller Connected first.

### D-014 Gyro reaches Unreal as time slices, one velocity per frame
Decision: every gyro sample is kept as the stretch of time since the sample before it, and each game frame plays back exactly the time it covers (`FGyroTimeSlicer`). When samples run out mid-frame the last velocity is held for at most 50 ms, and the held time is owed: the next samples pay it off instead of being played on top. Sliced once per engine frame and shared by Unreal's Rotation Rate and `JSL4U Get And Clear Accumulated Gyro`; `JSL4U Get IMU State` stays the latest report.
Why: report rates range from 80-90 a second (Pro Controller 2 on Bluetooth, below the frame rate) to 500 (on the cable, several per frame) (F-014). The latest sample repeated a stale velocity without limit when the controller went quiet and threw most samples away when it was fast. Summed over frames, slicing gives the turn the controller measured, at up to one sample of latency.
Origin: user's choice (R-037), approved on hardware 2026-11-06 in `LV_JoyShockGyroAim`: smooth on the cable, good but less so on Bluetooth
Not chosen: the latest report each frame (above); a plain average of the samples since the last frame -- weighs samples equally whatever time they cover and also repeats without limit. GyroWiki only says velocity times the time since the last sample; it does not cover reports slower than frames.

### D-015 Stick signs live in one table, and the stick getters return what the plugin sends
Decision: protocol readers leave every axis in the sign the hardware sends; `JoyShockStickTable.cpp` has one row per controller with each axis's sign, the sideways quarter turn of a Joy-Con half (L counter-clockwise, R clockwise) and who measured it. `JSL4UStickTable::PresentSticks` serves Unreal input and `JSL4U Get Left/Right Stick` and `Get Controller State` alike, so the getters read up = +1 on both sticks, a sideways Joy-Con turned into its grip. The sideways motion undo reads its direction from the same table.
Why: flips spread over readers and interface cancelled out more than once; the getters disagreed with input for sideways Joy-Cons and between families.
Origin: user's design (R-001), approved on hardware 2026-11-06 here; rows for the Switch 1 Pro, Joy-Con 2 and DualSense are derived and await Renan
Not chosen: getters matching what a game reads (right stick up = -1) -- that sign is the engine's viewport flip (F-015), downstream of the plugin.
Affects: CLAUDE.md, docs/input.md, docs/architecture.md

### D-016 The player slots a game assigned end with it; controllers and pairings stay
Decision: `UJoyShockSubsystem::Deinitialize` calls `ResetPlayerAssignmentsToAutomatic`, which drops every assigned slot and hands out automatic ones in handle order. Connections, Joy-Con pairings and a parked Pro Controller 2's slot are untouched.
Why: slots live in the module, so the next Play In Editor session, in any level, inherited the last one's choices (F-016), where a packaged game starts from automatic slots. Reconnecting to start clean would rerun init on live Bluetooth links.
Origin: user's choice (R-020), 2026-11-06; compiled only, since nothing in the demo assigns a slot any more
Not chosen: only documenting it; separating joined pairs too -- joining is the player's gesture.
Affects: docs/players.md

### D-017 A demo level that changes who drives a pawn moves the pawn, not the controllers
Decision: in `LV_JoyShockGyroAim`, picking a controller finds or creates that controller's player (`Ensure Local Player For Controller`) and it possesses the level's single pawn; no controller changes player, so player lights stay put. The Game Mode spawns the pawn for player 0 only and split screen is off.
Why: the user wants each controller to keep its player and light; moving controllers between players also broke joined pairs and leaked into the next Play.
Origin: user, 2026-11-06, approved on hardware
Not chosen: `JSL4U Assign Controller To Player Index` per pick. Sharing a slot on purpose (R-023's buttons) is a different case: there the shared player is the point.
Affects: docs/demo.md

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

### F-008 Config/Input.ini does not register the Joy-Con 2 identifiers
Fact: `FJoyShockInterface` reports a Joy-Con 2 half as `JoyCon2Left` / `JoyCon2Right` (`JoyShockInterface.cpp`, the hardware-identifier switch), but `Config/Input.ini` has `HardwareDevices` entries only for JoyConLeft, JoyConRight, SwitchProController, Switch2ProController, DualShock4, DualSense and JoyShockGamepad.
Source: code and config read, 2026-10-25
Verified: 2026-10-25

### F-009 What the Pro Controller 2 reports beyond input
Fact: battery voltage, percent and level read correctly over the cable (3.747 V, 62%, Medium) and over Bluetooth (3.652 V, 54%, Medium). The byte after the voltage is a charge state, not the low byte of a current (corrected 2026-11-06, from ndeadly's switch2_controller_research `hid_reports.md`): it read 0x34 on the cable while charging, 0x20 once the charge light went out, and 0 on Bluetooth. The "+0.52 A" read on 2026-10-31 was that 0x34; the battery current is the next two bytes, reported only with feature bit 0x20 and only by the Joy-Con 2. Since R-050 Is Charging is the charge state above 0x20, tested on the Pro Controller 2 on 2026-11-07: true while the charge light is on, false once it goes out with the cable in, false on Bluetooth. The magnetometer is present: over Bluetooth it reads non-zero and moves as the controller turns; over the cable it reads zero because the USB init does not ask for it (FEATSEL 0x27, against 0x94 over Bluetooth). The mouse fields read zero on both, as the Pro 2 has no mouse sensor. The temperature read 25.07-25.12 and did not warm in the hand; F-017 has what it turned out to be.
Source: the user's Pro Controller 2 with a test Blueprint printing JSL4U Get Switch 2 Sensors and the controller info, PIE; `JoyShock_Switch2.cpp` read for the FEATSEL values
Verified: 2026-10-25
Affects: R-031, R-032, F-017, docs/hardware.md

### F-010 A Switch 2 on Bluetooth stays with the editor in Standalone
Fact: with the editor connected to a Pro Controller 2 over Bluetooth, a Standalone game launched from the editor logged "0 controller(s) already connected" and never tried to connect it; PIE worked. The Bluetooth session belongs to the process that opened it and a connected controller stops advertising, and unlike the USB interface nothing hands it over.
Source: editor and Standalone logs (`Saved/Logs/BossRush.log`, `BossRush_2.log`), 2026-10-25 23:15
Verified: 2026-10-25
Affects: R-033, docs/switch2.md

### F-011 A Pro Controller 2 moved from Bluetooth to the cable keeps streaming report 0x09
Fact: plugging the cable into a Pro Controller 2 connected over Bluetooth retired the Bluetooth controller; the USB init then got an ack for every command and the first USB report was still 0x09, so the parser dropped every report and the controller was never announced, until PIE ended and it was retired as "never delivered input". Matches FofuxoJoySense4Unity's F-010, where the same set-up run again a few seconds later switched it to 0x05.
Source: `Saved/Logs/BossRush.log`, 2026-10-29 22:05:53, the user's Pro Controller 2
Verified: 2026-10-29
Affects: R-004

### F-012 Switch2Connect reads a battery current after the voltage
Fact: Switch2Connect's input parser reads the two bytes after the battery voltage (BLE offsets 33-34; packet[34..35] in this plugin's indexing) as `battery_current`, unsigned, divided by 100, and never uses the value. Nothing in it reads a charging flag. Its first byte is in fact a charge state, per ndeadly's research (see F-009); this plugin copied the misreading until R-050.
Source: https://github.com/TommyWabg/Switch2Connect `src/controller.py`, the input-report parser
Verified: 2026-10-29
Affects: R-031

### F-013 The first USB read of a Pro Controller 2 cabled before a Standalone starts can fail
Fact: with the controller on the cable before a Standalone started beside the editor, the first transfer on the freshly opened WinUSB interface -- the SPI read of block 0x013000 -- failed silently, while every read after it in the same init succeeded. Plugged in after the start, it never failed. `init_switch2` now retries that read once and logs "the info block read failed on the first try"; the retry succeeded. Cause not proven; a reply left in the pipe by its previous owner fits.
Source: BossRush_2.log, reproduced on purpose by the user and fixed in Standalone, 2026-11-06
Verified: 2026-11-06
Affects: D-012

### F-014 Input reports per second, measured in the plugin
Fact: Pro Controller 2 500 a second on the cable and 80-90 on Bluetooth; DualShock 4 500 steady on the cable and around 500, fluctuating, on Bluetooth; Joy-Con 50-70. On Bluetooth the rate fluctuates a little even with the controller still.
Source: `JSL4U Get Reports Per Second` in the demo panel, in Standalone, read by the user
Verified: 2026-11-06
Affects: D-014

### F-015 Unreal negates Gamepad Right Y for every gamepad before the game sees it
Fact: `FSceneViewport::OnAnalogValueChanged` passes `-Value` for `EKeys::Gamepad_RightY` and the raw value for every other key, so with the right stick up a game reads -1 from an Xbox pad and from every controller of this plugin alike; the left stick reads +1. `Get Input Analog Key State` and Enhanced Input both read after it.
Source: Engine/Source/Runtime/Engine/Private/Slate/SceneViewport.cpp:1352 (UE 5.8); measured in `LV_JoyShockGyroAim` on Pro Controller 2, DualShock 4 and a Joy-Con pair
Verified: 2026-11-06
Affects: D-015

### F-016 Controllers, player slots and Joy-Con pairings survive from one PIE session to the next
Fact: the module, its controllers, `PlayerSlotByPrimary` and pairings live as long as the editor; at the end of a Play only `UJoyShockSubsystem` goes. After the gyro-aim level put every controller but one on player 1, the next Play of the multiplayer level started with them all there and a pair still joined.
Source: code (`JoyShockSubsystem.cpp`, `JoyShockPlayerAssignment.cpp`) and the session start log line, on hardware, R-020
Verified: 2026-11-06
Affects: D-016

### F-017 The Pro Controller 2 temperature is in bytes 47-48, on a scale not confirmed
Fact: bytes 47-48 of report 0x05 (0x2E without the report id) are a temperature. Read whole every 2 s on the user's Pro Controller 2, they went from 3 just after switching on, in a room of about 25 °C, to 8 after 7 minutes charging on the cable, in clean steps of 1 that only flickered at the boundary. ndeadly documents the field as a signed 16-bit deviation from 25 °C at 1/126.9 °C per unit, which is the plugin's formula (`25 + raw / 127`); by it, those 7 minutes warmed the controller by 0.04 °C, and steps that clean do not fit that resolution. The plugin keeps ndeadly's scale; the real one, likely half a degree or a degree per unit, needs a second known temperature to confirm.
Source: temporary whole-report dump in `parse_switch2_report`, `Saved/Logs/BossRush.log`, 2026-11-06 23:18-23:25; https://github.com/ndeadly/switch2_controller_research `hid_reports.md`
Verified: 2026-11-06
Affects: JSL4U Get Switch 2 Sensors (Temperature Celsius)
