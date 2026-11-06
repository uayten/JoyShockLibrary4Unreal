# Decisions

<!-- project-docs: only what holds now. Next ID: D-012 · Next fact: F-013 -->

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
Affects: R-023, R-024

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

### D-010 The editor holds controller input back while none of its windows is in front
Decision: in the editor, with no editor window in front and no Play-In-Editor session, `SendControllerEvents` keeps reading every controller but dispatches nothing, releasing held input on the frame it stops. Games keep Unreal's default and answer from the background.
Why: a Standalone game launched from the editor is a second process on the same controllers, and Unreal dispatches gamepad input regardless of focus (`Slate.RequireFocusForGamepadInput` is off), so the controller drove the editor's menu navigation behind the game. Staying connected keeps the player slot for when the editor returns; PIE runs inside the editor process, so holding back there would hold back the game too.
Origin: agent's choice, 2026-11-06
Affects: R-044

### D-011 With a game running beside it, the editor writes nothing to any controller
Decision: while the D-008 flag says a game with the plugin is running, the editor's polling threads send no player lights, light bar or rumble, and it skips the DualShock 4's connect-time light. When the game closes, each thread sends the editor's player light and rumble once more; the HOME light clear is not repeated.
Why: both processes read the same HID controllers and each keeps its own player list, so both wrote player lights: a Joy-Con pair joined in both at once showed two different players, whichever write landed last (2026-11-06). The game is what is being played.
Not chosen: writing only from the process whose window is in front (a game answers from the background, D-010, and would lose its lights whenever the editor is clicked).
Origin: agent's choice, 2026-11-06
Affects: R-047, D-008

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
Fact: battery voltage, percent and level read correctly over the cable (3.747 V, 62%, Medium) and over Bluetooth (3.652 V, 54%, Medium). The report has no charging flag, but the two bytes after the voltage read +0.52 A on the cable at 3.746 V and exactly 0 on Bluetooth at 3.655 V (2026-10-31), so Is Charging is current above zero. The magnetometer is present: over Bluetooth it reads non-zero and moves as the controller turns; over the cable it reads zero because the USB init does not ask for it (FEATSEL 0x27, against 0x94 over Bluetooth). The mouse fields read zero on both, as the Pro 2 has no mouse sensor. The temperature reads 25.07-25.12 and does not warm in the hand, so it is not a temperature as parsed.
Source: the user's Pro Controller 2 with a test Blueprint printing JSL4U Get Switch 2 Sensors and the controller info, PIE; `JoyShock_Switch2.cpp` read for the FEATSEL values
Verified: 2026-10-25
Affects: R-031, R-032, R-034, docs/hardware.md

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
Fact: Switch2Connect's input parser reads the two bytes after the battery voltage (BLE offsets 33-34; packet[34..35] in this plugin's indexing) as `battery_current`, unsigned, divided by 100, and never uses the value. Nothing in it reads a charging flag.
Source: https://github.com/TommyWabg/Switch2Connect `src/controller.py`, the input-report parser
Verified: 2026-10-29
Affects: R-031
