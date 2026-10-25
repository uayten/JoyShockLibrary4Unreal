# Roadmap

<!-- project-docs: pending work only; approved items are deleted. Order = priority. Next ID: R-026 -->

## Testing

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

### R-013 [adjustment] A short README, the details in docs/, and a Portuguese version
Adjusts: README.md
Expected: the repository's front page is short -- what the plugin does, which controllers, how to install and start -- and each part of the plugin has its own page in `docs/` for whoever wants to go deeper. There is a Portuguese README alongside the English one, with the same content.
How:
- Portuguese first: `README.pt-BR.md` is written and reviewed before anything else; the English version follows it in R-014.
- `README.md` stays exactly as it is until then.
- Each long README section becomes a `docs/<topic>.md`, leaving a sentence and the link. Proposed topics (agent's choice): hardware, getting-started, input, players, output, switch2, blueprint-nodes, troubleshooting, architecture.
- The references to README sections in `CLAUDE.md` and in this roadmap follow the text to where it moves.
- *Planned future updates* leaves the README: pending work belongs here (agent's choice).
- The `docs/` pages are English only; both READMEs link to them (D-002).

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
Adjusts: stick normalisation (README > *Where to change what*).
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

### R-004 [adjustment] A Switch 2 that keeps its Bluetooth report mode on the cable must be set up again
Adjusts: Switch 2 handling in `pollIndividualLoop` (`JoyShockPolling.cpp`), next to the existing init retry.
Expected: plugging the cable into a Switch 2 Pro Controller connected over Bluetooth ends with it reading over USB within a couple of seconds, every time.
Facts to start from (FofuxoJoySense4Unity F-010, 2026-10-12):
- The Bluetooth init sets input report mode 0x09 (`03:0A` with `09`). The controller keeps that mode when the cable is plugged in: over USB it then sends report 0x09, whose USB layout is not 0x05's (the left stick sits where 0x05 has buttons).
- The USB init that runs on plugging in completed with every command answered and the mode still stayed 0x09: it landed while the controller was still leaving the radio. Run again by hand a few seconds later, it switched to 0x05 at once.
- `parse_switch2_report` drops anything that is not 0x05, so no wrong buttons reach the game -- but the existing retry in `pollIndividualLoop` only fires while `sw2_init_succeeded` is false, and here the init succeeded. The likely result is a controller that stays silent on the cable. Not verified on this plugin; the Unity port saw the 0x09 reports directly.
- The Unity port re-runs the USB setup, at most every 0.75 s, for as long as non-0x05 reports arrive. The Switch 1 branch already does the same kind of recovery for report 0x3F (`recover_switch_mode`).
- In that test plugging the cable dropped the Bluetooth link, and pulling it switched the controller off; a button press reconnected it over Bluetooth.
Tests:
- Connected over Bluetooth, plug the cable in: the controller works over USB within about two seconds. Repeat a few times.
- Pull the cable: the controller comes back over Bluetooth (after a button press, if it switched off).

### R-005 [adjustment] Open the GATT session before looking up the Switch 2 service
Adjusts: `Switch2Ble::Connect` (`Switch2Bluetooth.cpp`).
Expected: a controller woken by a button connects on the first advertisement, also right after it leaves its cable.
Facts to start from (FofuxoJoySense4Unity, 2026-10-13):
- `Connect` looks up the service with `BluetoothCacheMode::Uncached` before creating the `GattSession`, and fails at once when that lookup comes back empty. In the Unity port, with the same order, a controller woken by a button right after its cable was pulled failed every attempt with "does not expose the Switch 2 service".
- After moving the session (with `MaintainConnection(true)`) before the lookup, and retrying the lookup three times 500 ms apart with the last one `Cached`, the same case connected. It is not proven which of the two changes did it, and that test ran in a fresh process, so the earlier failures may also have come from the leak in R-003.
Tests:
- Plug the cable in and pull it, then wake the controller with a button: it reconnects over Bluetooth. Repeat a few times.

### R-006 [adjustment] Recover a Switch 1 controller from report 0x3F once, not on every report
Adjusts: the 0x3F recovery from PR #7 (`recover_switch_mode` in `JoyShock.cpp`, called from `pollIndividualLoop` in `JoyShockPolling.cpp`).
Expected: a Joy-Con or Pro Controller that drops into the simple 0x3F mode over Bluetooth gets its motion back, and a controller that stays in 0x3F is not sent configuration commands over and over, so it does not lose its Bluetooth link.
Facts to start from (code review, 2026-10-25, not checked on hardware):
- Every 0x3F report triggers subcommands 0x03 (report mode 0x30) and 0x40 (IMU on), plus a Warning in the log. Nothing limits how often: a controller that ignores the 0x03 gets both on every report it sends, for as long as it stays connected.
- These are configuration subcommands on a live Bluetooth stream, the thing the plugin's CLAUDE.md lists as having dropped working controllers four times. Both go out as write-only (`write_subcommand`), which is what the IMU repair also uses, so the risk is the repetition rather than the form.
- In 0x3F the report carries no IMU, so the IMU-repair toggle in the same loop (0x40 off, then on) also runs: two mechanisms writing 0x40 at the same time.
- A failed write marks the motion sensor as blocked (`note_output_result`), which fires the "another application is holding this controller" event even when nothing else is.
- R-004 points at this function as the model for its own recovery, so whatever limit is chosen here should apply there too.
Tests (Joy-Con here; a Switch 1 Pro Controller is Renan's):
- Over Bluetooth, play for a few minutes with gyro in use: gyro keeps working and the log shows at most one 0x3F line per episode.
- Turn a Joy-Con off and on while the game runs: it reconnects with gyro and does not disconnect again by itself.

### R-007 [adjustment] Stop re-running the Switch 2 init every 2 seconds on a controller that streams
Adjusts: the Switch 2 init retry in `pollIndividualLoop` (`JoyShockPolling.cpp`), `init_switch2` and `init_switch2_bluetooth` (`JoyShock_Switch2.cpp`).
Expected: a Switch 2 whose first init went wrong keeps responding without pauses, on cable and on Bluetooth; the retry only happens when it can actually help.
Facts to start from (code review, 2026-10-25, not checked on hardware):
- While `sw2_init_succeeded` is false, the polling thread re-runs the whole init every 2 s, forever, on a controller that is already sending input. Over USB a single failed `WinUsb_WritePipe` among the ten commands is enough; over Bluetooth, three unanswered commands in a row.
- The retry exists for the WinUSB lease that the editor and its Standalone child pass between them. Over Bluetooth there is no lease, but the same retry runs `init_switch2_bluetooth` anyway: the configuration exchange on a live Bluetooth stream that the plugin's CLAUDE.md forbids.
- Each retry blocks the polling thread: over Bluetooth every command waits up to 1 s for its answer, and each of the calibration reads tries three times. Input queued meanwhile is capped at 32 reports, oldest dropped. R-012 makes each refused command cost less, but not the repetition.
- The timeout branch of the same loop says the Switch 2 handshake "resets the stream so it never settles".
- R-004 needs a retry that fires even after a successful init; the two should be designed together.
Tests (Pro Controller 2):
- On cable and on Bluetooth: play for a few minutes: no freezes in the input, and the log shows the init sequence once per connection.
- Open the editor and play in Standalone with the controller on the cable: rumble and player lights work in the Standalone window.

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
- The README's "no blocking HID under `_connectedLock`" rule was written for hidapi, but WinRT teardown is the same kind of risk. The transport-switch branches of the same loop already drop the lock before their slow work.
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

### R-014 [adjustment] Update README.md
Adjusts: README.md
Expected: README.md with the same content as the current README.pt-BR.md.
Depends on: R-013
