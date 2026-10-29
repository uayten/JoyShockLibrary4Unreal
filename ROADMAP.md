# Roadmap

<!-- project-docs: pending work only; approved items are deleted. Order = priority. Next ID: R-035 -->

## Testing

### R-006 A Joy-Con in 0x3F mode is recovered a few times, not on every report
1. Four Joy-Cons on Bluetooth, play for a few minutes with gyro in use: gyro keeps working, and if the log shows "0x3F mode reset detected" it is followed by at most two "still in 0x3F" lines per episode.
2. Turn a Joy-Con off and on while the game runs: it reconnects with gyro and does not disconnect again by itself.

### R-007 The Switch 2 init runs once per connection
1. Pro Controller 2 on the cable, then on Bluetooth: play for a few minutes. No freezes in the input, and the log shows "Running Switch 2 ... init sequence" once per connection.
2. With the editor open, play in Standalone with the controller on the cable: rumble and player lights work in the Standalone window.

### R-004 A Pro Controller 2 moved from Bluetooth to the cable keeps working
1. With the Pro Controller 2 on Bluetooth, in PIE, plug its cable in: within about two seconds it works again over USB. The log (Window > Output Log) may show "streaming report 0x09 on USB instead of 0x05; running the USB set-up again" and then "now streaming report 0x05".
2. Repeat a few times. If the log ever says "still streaming report 0x09 on USB after 10 set-ups", that is a failure: send the log.
3. Pull the cable: it comes back on Bluetooth (after a button press, if it switched off).

### R-015 Joy-Con motion comes back by itself when the sensor starts dead
1. Connect four Joy-Cons over Bluetooth, one after the other, and open the log (Window > Output Log): every Joy-Con shows moving gyro on its controller mirror within a few seconds.
2. If one shows no gyro: the log says "toggling the IMU enable in-stream" and then "resumed IMU samples" for that controller, and its gyro starts moving.

### R-016 A controller moves between cable and Bluetooth without disconnecting
1. With a DualShock 4 on Bluetooth, plug its cable in: it keeps working, with the same player number.
2. Pull the cable: it goes back to Bluetooth and keeps working, with the same player number.
3. Repeat plugging and pulling ten times, quickly: the editor never freezes and the controller never needs to be turned off and on.

### R-017 Joy-Con 2 calibration read at every connection (Renan)
1. Connect each Joy-Con 2 half over Bluetooth ten times: every time the log shows a stick calibration line, and the stick reaches the edge of its range on the controller mirror.

## In progress

## To do

### R-003 [adjustment] A failed Bluetooth connection must close what it opened
Adjusts: `Switch2Ble::Connect` (`Switch2Bluetooth.cpp`).
Expected: every way out of `Connect` short of success closes the GATT service, the session (with `MaintainConnection(false)`) and the device, so a failed attempt leaves nothing behind.
Facts to start from (FofuxoJoySense4Unity F-008, 2026-10-12):
- `Connect` returns `nullptr` when a characteristic is missing or a subscription is refused, without closing anything; the session was set to `MaintainConnection(true)` before that. The service it found is never closed on any path, success included, and `Disconnect` does not close it either.
- Measured in the Unity port, which had the same code: with the Nintendo service left open by one failed attempt, every later attempt -- in the same process or another -- got "sharing violation" from `OpenAsync(SharedReadAndWrite)` and "access denied" (status 3) from every characteristic lookup, although Windows reported the controller paired and access allowed. Closing the process that held it fixed it at once. The first failure feeds every later one, so a single bad attempt can leave the controller unusable until the editor restarts.
- The Unity port keeps the service on the connection, closes it in teardown, and runs the teardown from a scope guard on every failure path.
Tests:
- Connect, disconnect, connect again several times in one editor session: every connection succeeds.
- Force a failed attempt (for example, turn the controller off right after it starts advertising), then connect normally: it succeeds without restarting the editor.

### R-001 [adjustment] One stick Y sign per layer, across controller families
Adjusts: stick normalisation (`docs/architecture.md` > *Where to change what*).
Expected: the low-level JSL state (`simple_state`, the direct getters and the Blueprint nodes that read it) reports the right stick's Y with the same sign as the left one on every family, and the comments say what the code actually does. `FJoyShockInterface` then presents the sticks without per-family corrections.
Facts to start from:
- Switch 2 Pro Controller over USB: in the raw report both sticks grow upwards; no axis needs negating. Measured on hardware with the Stick Overlay of FofuxoJoySense4Unity (its F-006), 2026-10-08.
- `parse_switch2_report` (`InputHelpers.cpp`, the `bWhole` block) negates the right Y "to make up positive on both sticks". Given the fact above, it does the opposite: after it, the right stick reads up as negative.
- `FJoyShockInterface` (`JoyShockInterface.cpp`, end of `PresentSticks`) negates the right Y again for every non-Sony pad (`bSonyPad ? State.stickRY : -State.stickRY`). The two negations cancel, which is why Unreal sees the right sign. Its own comment says the low-level layer disagrees between Sony and the other families.
- The Switch 1 Pro Controller branch (`InputHelpers.cpp`, `left_right == 3`) adds the same negation with the same reasoning; the right Joy-Con is corrected once, in the interface. Check both against hardware before touching them.
Before changing: anything reading the low-level state directly (`JoyShockBlueprintLibrary.cpp` builds `RightStick` from `stickRY`) will flip sign. Needs a Switch 2 Pro, a Switch 1 Pro, a right Joy-Con and a Sony pad to verify, tested in Standalone.

### R-002 [adjustment] Over Bluetooth, only take Switch 2 controllers in pairing mode or paired with this PC
Adjusts: `discover_bluetooth_switch2` (`Private/JoyShockEnumeration.cpp`) and the advertisement suppression in `Switch2Bluetooth.cpp`.
Expected: a Switch 2 controller is connected only when its advertisement carries no host (SYNC held: it is then bonded to this PC, as today) or carries this PC's adapter address. One paired with another device -- a Switch 2 console in the same room -- is left alone, so a button pressed to use the console does not take the controller away from it; holding SYNC on it still pairs it with the PC.
Facts to start from (measured with FofuxoJoySense4Unity, which already does this -- its D-008 and F-011, 2026-10-13):
- In pairing mode the advertisement's reconnect address (manufacturer data bytes 10-15) is 0. After the bond commands, a controller woken by a button advertises this PC's adapter address there, the same value `Switch2Ble::GetHostAddress` returns, byte order included. So the comparison works as is.
- Today `discover_bluetooth_switch2` connects whatever advertises and only uses the reconnect address to decide whether to bond.
- `OnAdvertisementReceived` in `Switch2Bluetooth.cpp` suppresses an address forever once it has been offered, until a connection to it ends. A controller turned down by the new check would then never be offered again in the session, not even after its owner holds SYNC. It needs to be offered again after a short delay instead (FofuxoJoySense4Unity added `JsBle_SuppressFor(address, 2000)` for this).
Tests:
- A controller already paired with the PC reconnects on a button press, as before.
- A controller paired with a Switch 2 console: pressing a button does not connect it to the PC, and the log says once that it is paired with another device; holding SYNC then connects it and pairs it with the PC.
- A brand-new controller in SYNC mode connects and pairs, as before.

### R-005 [adjustment] Open the GATT session before looking up the Switch 2 service
Adjusts: `Switch2Ble::Connect` (`Switch2Bluetooth.cpp`).
Expected: a controller woken by a button connects on the first advertisement, also right after it leaves its cable.
Facts to start from (FofuxoJoySense4Unity, 2026-10-13):
- `Connect` looks up the service with `BluetoothCacheMode::Uncached` before creating the `GattSession`, and fails at once when that lookup comes back empty. In the Unity port, with the same order, a controller woken by a button right after its cable was pulled failed every attempt with "does not expose the Switch 2 service".
- After moving the session (with `MaintainConnection(true)`) before the lookup, and retrying the lookup three times 500 ms apart with the last one `Cached`, the same case connected. It is not proven which of the two changes did it, and that test ran in a fresh process, so the earlier failures may also have come from the leak in R-003.
Tests:
- Plug the cable in and pull it, then wake the controller with a button: it reconnects over Bluetooth. Repeat a few times.

### R-008 [adjustment] A controller that disconnects while the editor is closing must not crash it
Adjusts: `ShutdownAllDevices` (`JoyShockLibrary.cpp`) and the disconnect and timeout branches of `pollIndividualLoop` (`JoyShockPolling.cpp`).
Expected: closing the editor never crashes, even when a controller is unplugged, turned off or drops at that same moment.
Facts to start from (code review, 2026-10-25, not reproduced):
- `ShutdownAllDevices` sets `cancel_thread` and clears `delete_on_finish`/`remove_on_finish` on every device, then waits for `thread_exited`. Those three are plain `bool`s, written without any lock.
- A polling thread that is already past its `cancel_thread` check -- its read just returned -1, or it hit the tenth timeout -- takes `_connectedLock` after shutdown has emptied the maps and sets `delete_on_finish = true`. It then frees the device itself and never sets `thread_exited`. Shutdown keeps reading `thread_exited` from freed memory and, after its 3 s deadline, may join and delete that device a second time.
- Likely fix: the two branches check `cancel_thread` after taking the lock and leave the delete to shutdown, and the flags become atomic.
- The kind of crash Renan's debugger stops on and the user's editor hides, since it was already closing.
Tests (any controller; worth asking Renan, under the debugger):
- With a controller connected, close the editor and pull its cable (or turn it off) at the same moment. Repeat several times: the editor always closes cleanly.

### R-009 [adjustment] Calibrate the Switch 2 sticks over USB with the same checks as over Bluetooth
Adjusts: stick calibration in `init_switch2` (`JoyShock_Switch2.cpp`).
Expected: a Pro Controller 2 whose stored calibration is blank or invalid gets the same centred sticks on the cable as over Bluetooth.
Facts to start from (code review, 2026-10-25):
- The Bluetooth init checks each calibration block with `AxisIsPlausible` (centre near the middle, a real range in all four directions) and falls back to fixed centres when a block fails it. The USB init only checks the first three bytes of the user block for all-ones, and applies the factory block with no check at all.
- The Bluetooth init's own comment describes the consequence: an erased block parses into a centre of 4095, and the stick reads as held to one side.
- `ReadStickCalFrom`'s check can be shared rather than written a second time.
Tests (Pro Controller 2):
- On cable and on Bluetooth, sticks at rest: both read centred, and the log line for the stick calibration says the same thing on both transports.

### R-010 [adjustment] Close a Switch 2 Bluetooth connection outside the connected lock
Adjusts: `finish_polling_thread` (`JoyShockPolling.cpp`) and the race-loser discard in phase 3 of `ConnectDevices` (`JoyShockEnumeration.cpp`).
Expected: turning off a Switch 2 that is on Bluetooth never freezes the game, not even briefly.
Facts to start from (code review, 2026-10-25, not measured):
- Both places run `delete jc` while holding `_connectedLock`, which every device getter on the game thread waits for. For a Bluetooth Switch 2 the destructor calls `Switch2Ble::Disconnect`, which closes the connection-parameters request, the `GattSession` and the `BluetoothLEDevice` synchronously.
- The "no blocking HID under `_connectedLock`" rule in `docs/architecture.md` was written for hidapi, but WinRT teardown is the same kind of risk. The transport-switch branches of the same loop already drop the lock before their slow work.
Tests (Pro Controller 2 on Bluetooth):
- While moving a stick on another controller, turn the Switch 2 off: the other controller never stops responding. Repeat a few times.

### R-011 [adjustment] Bluetooth callbacks must not look the module up during shutdown
Adjusts: the discovery callback and the scan start in `StartupModule` (`JoyShockLibrary4Unreal.cpp`).
Expected: closing the editor with a Switch 2 advertising nearby, or right after opening it, never crashes.
Facts to start from (code review, 2026-10-25, not reproduced):
- The discovery callback runs on a WinRT thread and calls `IsAvailable()` (which asks `FModuleManager`), then `GetInstance()`. Once `ShutdownModule` has cleared `GLiveJoyShockModule`, `GetInstance` falls back to `LoadModuleChecked`, the very call the plugin's CLAUDE.md says fails its own check off the game thread during shutdown. `Switch2Ble::Shutdown` clears the callback before that, but a callback already running is not waited for.
- The startup thread checks `bShuttingDown` only before `IsSupported()`, which blocks. If the editor closes in that window, `SetDiscoveryCallback` and `StartScan` run after `Switch2Ble::Shutdown` and leave a scan running with a callback into a module that is gone.
Tests (Pro Controller 2):
- Open the editor and close it as soon as it finishes loading, with the controller advertising (hold SYNC). Repeat a few times: it always closes cleanly.

### R-012 [adjustment] A Switch 2 command the controller refuses should fail at once
Adjusts: `Switch2Ble::SendCommand` (`Switch2Bluetooth.cpp`).
Expected: a refused Bluetooth command costs one round trip instead of a second's wait, so connecting a Switch 2 takes no longer than it needs to.
Facts to start from (code review, 2026-10-25):
- `SendCommand` accepts only a reply whose status byte is 0x01. A reply to the same command with any other status is thrown away and the wait goes on to the full timeout (1 s by default).
- The init expects refusals ("not all of them implement all of it"), and each calibration read tries three times, so one refusal can cost up to 3 s. Combined with the retry in R-007, the polling thread spends most of each cycle waiting.
Tests (Pro Controller 2 and, with Renan, a Joy-Con 2):
- Connect over Bluetooth and read the log: no command takes about a full second to be reported as not acknowledged, and the controller responds within a couple of seconds of connecting.

### R-018 [adjustment] The demo's controller-name widget follows controller changes
Adjusts: `WBP_ControllerName` (demo, `Content/`).
Expected: in Standalone, every character's widget shows the name of the controller driving it, including the first character and controllers that connect later.
Facts to start from (diagnosed 2026-07-23):
- The widget asks once, at Event Construct, which controllers belong to its player. In Standalone the first character is possessed before enumeration has found any controller, so it gets none and never asks again. PIE hides it because the editor process enumerated long before.
- Fix prescribed, not applied: the widget subscribes to Listen For Controller Changes on the JoyShock Subsystem (and optionally Listen For Joy-Con Pairing Changes) and re-runs the name lookup on each event. The user edits the Blueprint.
Tests:
- In Standalone with two controllers: both widgets show their controller's name; turn one controller off and on: its widget follows.

### R-019 [adjustment] The demo creates one local player per controller, no more
Adjusts: the demo's player initialiser.
Expected: with one controller connected there is one local player, and the controller drives player 0.
Facts to start from (log of 2026-07-22, not fixed):
- With one controller, the initialiser created two local players and put the controller on index 1, leaving player 0 without one.
- Suspected: the branch "first controller reuses player 0, the rest create a new one" does not work, or the handler runs twice (an old ForEach plus Listen For Controllers). The target index should come from an idempotent counter per Connection Id, not from a live count.
- Proven by measurement, not to re-investigate: the plugin dispatches the second controller's input correctly (F-004).
Tests:
- In Standalone, one controller: one character, driven by it. Connect a second: a second character appears, driven by the second controller.

### R-020 [research] Are the plugin's controllers cleared between PIE sessions?
Expected: answer whether `_joyshocks` and the player slots survive from one Play In Editor session to the next, and whether that matters. Counts as an answer: a log of two PIE sessions in a row with the same controllers, showing their ids and player slots.
Facts to start from: `JslDisconnectAndDisposeAll` exists but may not be wired to the end of PIE (noted 2026-07-19).

### R-021 [feature] Joy-Con strap accessory in the demo
Expected: the demo has a model of the strap accessory that slides onto a Joy-Con's rail, for sideways one-handed play.
How: the user models it.

### R-022 [feature] Joy-Con Grip model with both Joy-Cons seated
Expected: the demo's Joy-Con Grip model shows both Joy-Cons seated in it, rather than the grip alone.
How: the user models it.

### R-023 [feature] A demo pawn driven by several controllers at once
Expected: the demo has a pawn that one or more controllers can drive at the same time.
How:
- Composed in the game, not in the plugin: one pawn reads several Connection Ids and is fed by all of them. The plugin's Joy-Con join already merges a left and right half into one player slot (agent's choice).
Open: or should the plugin generalise the join to any set of controllers?

### R-024 [feature] Demo HUD buttons to create, delete and reassign a pawn
Expected: the demo HUD has buttons to create a playable pawn, delete it, and assign it to another controller.
How:
- Reassigning keeps the plugin's player routing and remaps identity in the game ("player 1 is the red chef"), the way most party games do. The plugin has no node to pin a controller to a chosen player slot: its player index is what routes input, so `Create Player` must be given that index (agent's choice).
- Follows the demo architecture in D-004.
Depends on: R-023

### R-025 [feature] Joy-Con LED materials follow the controller
Expected: the player LEDs on the demo's controller models light up exactly like the physical controller's.
How: read `FJSL4UControllerInfo.PlayerLedNumber`, which is a position (1-4).
Open: the demo rework of 2026-07-19 already mirrors the pairing LED on the model -- is anything left of this item?

### R-026 [feature] Pro Controller and Xbox models in the demo's controller mirror
Expected: the demo's controller mirror has a model for the Nintendo Switch Pro Controller, the Nintendo Switch 2 Pro Controller and an Xbox pad; today only the Joy-Con pair, the Joy-Con grip, the DualShock 4 and the DualSense are modelled, and the Joy-Con 2 borrow the Switch 1 Joy-Con models.
How: the user models them.
Origin: README *Planned future updates*, moved here by R-013.

### R-027 [feature] Switch 2 battery percentage from a charge curve
Expected: a Switch 2 controller's Battery Percent follows a lithium cell's discharge curve instead of a straight line between 3.0 V and 4.2 V, so the number is worth showing and not only a bar.
Facts to start from: the controller reports its cell voltage and nothing else, and the voltage sags under load -- it reads low mid-rumble.
Origin: README *Planned future updates*, moved here by R-013.

### R-028 [adjustment] A controller that arrives must drive its character
Adjusts: the demo's player initialiser.
Expected: every controller that connects drives its character, without stopping and re-playing.
Facts to start from:
- Reported once with a Joy-Con 2: the mirror appeared and the motion readouts worked, but the pawn did not move. Stopping and re-playing fixed it, which is not an option in a packaged build.
- Not diagnosed. Suspected the demo's assignment path rather than the plugin, since the controller was visibly connected and reporting. May share a cause with R-019.
Origin: README *Planned future updates*, moved here by R-013.

### R-029 [adjustment] Register the Joy-Con 2 halves in Config/Input.ini
Adjusts: the plugin's hardware devices (`Config/Input.ini`).
Expected: Unreal knows a Joy-Con 2 half as a hardware device of its own, the way it knows a Joy-Con half, so a game's input settings can pick it out.
Facts to start from: F-008. `FJoyShockInterface` reports the halves as `JoyCon2Left` / `JoyCon2Right`, and `Config/Input.ini` has no entry for either. Which `SupportedFeaturesMask` they need is not settled: the Joy-Con entries use 9541, the Pro Controller 2 entry 9477.
Tests: Renan, with a Joy-Con 2 pair (no Joy-Con 2 in Brazil).

### R-031 [adjustment] Switch 2 controllers report charging
Adjusts: battery reporting (`docs/controllers.md` > *Battery*).
Expected: Is Charging on a Switch 2 controller turns true when its cable goes in and false when it comes out, and Wait For Battery Changes raises On Charging Changed for it.
Facts to start from: F-009. `parse_switch2_report` (`InputHelpers.cpp`) fills voltage, percent and level but never `battery_charging`; where the report carries charging is not known yet (F-006's Switch2Connect is the reference to check).
Tests: the user, with the Pro Controller 2 over the cable and over Bluetooth.

### R-032 [adjustment] The Switch 2 magnetometer over the cable too
Adjusts: the Switch 2 USB init (`JoyShock_Switch2.cpp`, the FEATSEL command).
Expected: the magnetometer of a Switch 2 controller reads over the cable as it does over Bluetooth.
Facts to start from: F-009. The USB init asks for features 0x27, the Bluetooth one for 0x94 (motion 0x04, mouse 0x10, magnetometer 0x80). The Bluetooth comment warns that asking for every feature turns on report fields the parser does not expect, read as stuck triggers -- add 0x80 alone and check the triggers.
Tests: the user, with the Pro Controller 2 over the cable: the magnetometer moves when the controller turns, and ZL/ZR read released at rest.

### R-033 [adjustment] A Switch 2 on Bluetooth reaches a Standalone game launched from the editor
Adjusts: `Switch2Bluetooth.cpp` / `discover_bluetooth_switch2`, next to the USB lease the editor and Standalone already pass between them.
Expected: with the editor holding a Switch 2 controller over Bluetooth, playing in Standalone gives the controller to the game, and it comes back to the editor when the game closes.
Facts to start from: F-010.
Tests: the user, with the Pro Controller 2 over Bluetooth, in Standalone launched from the editor.

### R-034 [research] Does the Pro Controller 2 report temperature, and where?
Expected: whether the Pro Controller 2's report carries a temperature, at which bytes and with which scale; the answer counts when the reading rises while the controller is held for a few minutes.
Facts to start from: F-009. The parser reads bytes 47-48 as `25 + raw / 127`; on the Pro 2 the raw value sits around 9-15 and only flickers.
