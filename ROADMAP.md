# Roadmap

<!-- project-docs: pending work only; approved items are deleted. Order = priority. Next ID: R-035 -->

## Testing

### R-002 Over Bluetooth, a Switch 2 paired elsewhere is left alone
1. The Pro Controller 2, already paired with this PC: turn it off, then press a button: it reconnects, as before.
2. Pair it with something else (a Switch 2 console, if one is at hand), then press a button near the PC: it does not connect, and the log says once "paired with another device ... Hold SYNC on it". Hold SYNC: it connects and pairs with the PC.
3. A controller in SYNC mode that was never paired connects and pairs, as before.
Step 2 needs a second host; without one, steps 1 and 3 still check nothing broke.

### R-032 The Switch 2 magnetometer over the cable
1. Pro Controller 2 on the cable: the Magnetometer box in the demo panel moves when the controller turns (before, it stayed at zero on the cable).
2. At rest, ZL and ZR read released -- check the controller mirror. If either reads held, that is the failure the item warns about: say which.
3. Then update docs/hardware.md and docs/switch2.md, which still say "Bluetooth only".

### R-011 Closing the editor with a Switch 2 advertising never crashes
1. Hold SYNC on the Pro Controller 2 so it advertises, open the editor and close it as soon as it finishes loading. Repeat a few times: it always closes cleanly.

### R-008 A controller dropping while the editor closes does not crash it
1. With a controller connected, close the editor and pull its cable (or turn it off) at the same moment. Repeat several times: the editor always closes cleanly. Worth asking Renan, under the debugger.

### R-010 Turning off a Switch 2 on Bluetooth never freezes the game
1. With the Pro Controller 2 on Bluetooth and another controller in use, keep moving a stick on the other one and turn the Switch 2 off: the other controller never stops responding. Repeat a few times.

### R-009 Switch 2 stick calibration checked the same way on cable and Bluetooth
1. Pro Controller 2 on the cable, then on Bluetooth, sticks at rest: both read centred on the controller mirror, and the log line "SW2 left/right stick cal" shows the same centre on both transports.

### R-003 A failed Switch 2 Bluetooth connection leaves nothing behind
1. Connect the Pro Controller 2 over Bluetooth, turn it off, connect it again -- several times in one editor session: every connection succeeds.
2. Hold SYNC and turn the controller off right after the lights start moving, so the attempt fails; then connect normally: it succeeds without restarting the editor.

### R-005 A Switch 2 reconnects over Bluetooth right after leaving its cable
1. Plug the Pro Controller 2 in and pull the cable, then wake it with a button: it reconnects over Bluetooth. Repeat a few times.

### R-012 A refused Switch 2 Bluetooth command fails at once
1. Connect the Pro Controller 2 over Bluetooth and read the log: the init lines come out quickly, none waits about a second before "was not acknowledged", and the controller responds within a couple of seconds of connecting. (A Joy-Con 2 too, with Renan.)

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

### R-001 [adjustment] One stick Y sign per layer, across controller families
Adjusts: stick normalisation (`docs/architecture.md` > *Where to change what*).
Expected: the low-level JSL state (`simple_state`, the direct getters and the Blueprint nodes that read it) reports the right stick's Y with the same sign as the left one on every family, and the comments say what the code actually does. `FJoyShockInterface` then presents the sticks without per-family corrections.
Facts to start from:
- Switch 2 Pro Controller over USB: in the raw report both sticks grow upwards; no axis needs negating. Measured on hardware with the Stick Overlay of FofuxoJoySense4Unity (its F-006), 2026-10-08.
- `parse_switch2_report` (`InputHelpers.cpp`, the `bWhole` block) negates the right Y "to make up positive on both sticks". Given the fact above, it does the opposite: after it, the right stick reads up as negative.
- `FJoyShockInterface` (`JoyShockInterface.cpp`, end of `PresentSticks`) negates the right Y again for every non-Sony pad (`bSonyPad ? State.stickRY : -State.stickRY`). The two negations cancel, which is why Unreal sees the right sign. Its own comment says the low-level layer disagrees between Sony and the other families.
- The Switch 1 Pro Controller branch (`InputHelpers.cpp`, `left_right == 3`) adds the same negation with the same reasoning; the right Joy-Con is corrected once, in the interface. Check both against hardware before touching them.
Before changing: anything reading the low-level state directly (`JoyShockBlueprintLibrary.cpp` builds `RightStick` from `stickRY`) will flip sign. Needs a Switch 2 Pro, a Switch 1 Pro, a right Joy-Con and a Sony pad to verify, tested in Standalone.

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
- Switch2Connect reads the two bytes after the voltage (packet[34..35] here) as `battery_current`, unsigned, divided by 100, and never uses it. Since 2026-10-29 the plugin reads them signed into `Battery Current` on Get Switch 2 Sensors (commit "read the Switch 2 battery current"); `battery_charging` is not derived from it yet.
- Next: show Battery Current in the demo panel and read it with the cable in and out. NodeScribe cannot insert a node into an existing exec chain, and `replace` refuses on this EventGraph (its reading loses two reconvergences), so the row needs a hand edit in the editor or a NodeScribe change. Then set `battery_charging` from the sign that shows up while charging.
Tests: the user, with the Pro Controller 2 over the cable and over Bluetooth.

### R-033 [adjustment] A Switch 2 on Bluetooth reaches a Standalone game launched from the editor
Adjusts: `Switch2Bluetooth.cpp` / `discover_bluetooth_switch2`, next to the USB lease the editor and Standalone already pass between them.
Expected: with the editor holding a Switch 2 controller over Bluetooth, playing in Standalone gives the controller to the game, and it comes back to the editor when the game closes.
Facts to start from: F-010.
Tests: the user, with the Pro Controller 2 over Bluetooth, in Standalone launched from the editor.

### R-034 [research] Does the Pro Controller 2 report temperature, and where?
Expected: whether the Pro Controller 2's report carries a temperature, at which bytes and with which scale; the answer counts when the reading rises while the controller is held for a few minutes.
Facts to start from: F-009. The parser reads bytes 47-48 as `25 + raw / 127`; on the Pro 2 the raw value sits around 9-15 and only flickers.
