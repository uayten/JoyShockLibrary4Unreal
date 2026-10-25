# Roadmap

<!-- project-docs: pending work only; approved items are deleted. Order = priority. Next ID: R-006 -->

## Testing

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
