# JoyShockLibrary4Unreal

This is a fork of JibbSmart's [JoyShockLibrary](https://github.com/JibbSmart/JoyShockLibrary), modified to integrate with Unreal Engine's input system as a plug-in. This allows your Unreal Engine games to support DualShock 4, DualSense (including Edge), Switch Pro, Joy-Con and Switch 2 Pro controllers natively, and use some of their exclusive features such as gyro and touchpad.

**It fills the gaps rather than replacing Unreal's input.** Anything a standard gamepad can do — buttons, sticks, triggers, rumble, motion — reaches your game through Unreal's own input system, as the same keys and the same nodes an Xbox pad already uses. The `JSL4U*` nodes exist only for what Unreal and Windows have no concept of: gyro calibration, the light bar, joining two Joy-Cons into one player, assigning a controller to a player. So write your game against the engine's APIs and it supports every gamepad, and reach for a `JSL4U*` node when you want something only these controllers can do.

## Contents

**Getting started**
- [Hardware support](#hardware-support)
- [Installation](#installation)
- [Demo level](#demo-level)
- [How much of this do I have to set up?](#how-much-of-this-do-i-have-to-set-up)

**Features**
- [Input events](#input-events)
- [Reacting to controllers](#reacting-to-controllers)
- [Controller assignment and local multiplayer](#controller-assignment-and-local-multiplayer)
- [Combining Joy-Cons into one player](#combining-joy-cons-into-one-player)
- [Motion and gyro](#motion-and-gyro)
- [Rumble](#rumble)
- [Battery](#battery)
- [Lights and player indicators](#lights-and-player-indicators)
- [Conflicts with other applications](#conflicts-with-other-applications)
- [Nintendo Switch 2 Pro Controller](#nintendo-switch-2-pro-controller)

**Reference**
- [Blueprint nodes](#blueprint-nodes)
- [Diagnostics and troubleshooting](#diagnostics-and-troubleshooting)
- [Questions](#questions)
- [Planned future updates](#planned-future-updates)
- [Credits](#credits)

## Hardware support

The current Unreal Engine 5.8 overhaul has been exercised with the hardware below. "Needs a regression
test" means the implementation is present and was working before the overhaul, not that the controller is
known to be broken. "Untested" means no unit has been available.

| Controller | Current validation |
| --- | --- |
| Joy-Con (L/R) | Gameplay input, solo-horizontal and joined presentation, motion, pairing/separation, player LEDs, battery and multiple simultaneous controllers tested. |
| Nintendo Switch 2 Joy-Con | Not implemented and untested. |
| Nintendo Switch Pro Controller | USB/Bluetooth input, motion, player LEDs and HD rumble are implemented, but untested since the overhaul. |
| Nintendo Switch 2 Pro Controller | USB gameplay input, calibrated sticks, motion, player LEDs and fixed-amplitude rumble tested. Bluetooth is not supported yet. |
| DualShock 4 | Bluetooth and USB gameplay input, stick conventions, motion, battery reporting and cable/Bluetooth transport switching tested. Battery percentages were checked against DS4Windows. Rumble could not be validated because the available controller's rumble hardware is broken. |
| DualSense / DualSense Edge | USB/Bluetooth input, motion, touchpad, light, player indicators and rumble are implemented, but need a regression test after the overhaul. Battery report offsets are unverified. |
| Xbox / other XInput pads | Handled entirely by Unreal, not by this plugin. The plugin only takes their player slots into account, so a JoyShock controller lands where a second Xbox pad would. That slot-sharing behaviour still needs a test with both kinds connected at once. |

## Installation
- Download or clone the JoyShockLibrary4Unreal repo from this GitHub page and add it to your game's Plugins folder. The path to the Content folder should look like this: `<project>/Plugins/JoyShockLibrary4Unreal/Content`.
- Make sure that JoyShockLibrary4Unreal is enabled in your project's .uproject file or Plug-in settings.
- On Windows, no other plug-in is needed (hidapi is bundled). On other platforms, the plug-in falls back to microdee's [HIDUE](https://github.com/microdee/HIDUE) plugin for HID communication, so add it to your project as well.

## Demo level

There's a demo level called LV_JoyShockDemo in the Content folder! You can find it in your Content Browser, as long as you enable showing Plugin Content in your Content Browser settings:
![Accessing Test Level](https://github.com/user-attachments/assets/920f87a8-6de6-4efd-a3bd-b8787ea1a9d4)

Connect one or more controllers and press Play. The level is a small local-multiplayer example: every detected controller gets a native Unreal `LocalPlayer`, `PlayerController` and possessed Character. Move each Character with its controller's left stick. Both Standalone and the PIE viewport are supported.

The level's existing `CameraActor` has **Auto Activate For Player** set to **Player 0**. Consequently the
demo keeps its fixed overview camera whether zero or several controllers are connected; possessing a
Character does not make the view jump to that Pawn. This uses Unreal's normal automatic camera management
instead of a `Set View Target with Blend` graph in the PlayerController.

The example deliberately uses standard engine responsibilities:

- `BP_JoyShockInitializer` is a lightweight Actor. On `BeginPlay` it places one **Wait For Controller Changes** node and one **Wait For Joy-Con Pairing Changes** node. Both feed the same idempotent function, which calls **Ensure Local Player For Controller**, adds the demo Mapping Context, and spawns and possesses a Character only when that player does not already have one. Replayed connections, reconnections and Joy-Con separations therefore all converge on the same code path without ever producing duplicate Pawns.
- `BP_JoyShockGameMode` owns only the `DefaultPawnClass` and `PlayerControllerClass`; its old manual player-management graph was removed. The level contains separate `PlayerStart` actors.
- `BP_JoyShockPlayerController` is intentionally empty. It demonstrates that controller routing, possession and Enhanced Input do not require a second input implementation in the controller Blueprint.
- `BP_JoyShockCharacter` contains only gameplay input and a one-tick-delayed **Set Focus to Game Viewport** after possession, which also makes newly created Slate users work in the PIE viewport.

The sensor diagnostics remain available as a companion to the multiplayer example. `BP_JoyShockControllerMirror` is a digital representation of the physical controller: it follows the controller's measured orientation so motion and coordinate conventions can be checked visually. `WBP_JoyShockHUD` and its supporting widgets display numeric motion/controller state and expose calibration controls. They consume the direct-device `JSL4U*` API because those measurements have no Xbox/Enhanced Input equivalent; they are not a replacement input path for buttons, sticks or gameplay.

The Mirror and HUD are deliberately not responsible for controller discovery, Local Player creation, possession or Mapping Context setup. The Initializer owns that lifecycle, so a project can remove the visual diagnostics without changing gameplay routing.

The demo automatically spawns one Mirror per physical connection and passes the complete
`FJSL4UControllerInfo` into its Expose-on-Spawn `Controller` property. The Initializer keeps a
`ConnectionId → BP_JoyShockControllerMirror` map; disconnecting destroys only that connection's actor.
`ConnectionId`, rather than `DeviceId` or `PlayerIndex`, is intentional: DeviceIds can be reused after
reconnecting, while joined Joy-Con halves share a PlayerIndex but remain two physical motion sensors and
therefore two Mirrors. No Mirror is placed manually in the level.

The HUD is rebuilt only when the connected set changes, then receives the current DeviceId-to-Mirror map;
its sensor values continue updating normally between those rare changes. The Mirror never enumerates
controllers or creates Local Players. `FJSL4UControllerInfo` also
exposes capability flags (`Has RGB Light`, `Has Player Indicator`, `Has Motion Sensors`, `Has Touchpad`,
`Has Rumble`) so diagnostic widgets can enable their controls without hard-coding controller-type switches.

## How much of this do I have to set up?

Three levels, and most projects only ever need the first.

**1. Nothing.** Buttons, sticks, triggers, the touchpad, rumble and motion all arrive through Unreal's own input system, as the same keys an Xbox pad produces. Install the plugin and a project already built for gamepads supports DualShock 4, DualSense, Joy-Cons and Pro Controllers — existing Input Mapping Contexts, `Play Force Feedback Effect` and everything else keep working untouched. The only thing you bind by hand is what has no Xbox equivalent (motion, and the extra buttons listed below), and even then you are binding *engine* keys, not plugin keys.

**2. Reacting to controllers coming and going.** Only needed for a controller-assignment screen, per-controller UI, or local multiplayer. Place a **Wait For Controller Changes** node and handle its `On Connected` pin; see [Reacting to controllers](#reacting-to-controllers) for why this is a single node rather than a binding.

**3. Local multiplayer.** For each `Controller` delivered by `On Connected`, call **Ensure Local Player For Controller** with `bCreateIfMissing` enabled. It reuses player 0 for the first controller, creates only the missing local players through Unreal's native `FPlatformUserId` API, repairs any mapper reassignment caused by player creation, and returns the correctly typed **Player Controller** plus its local-player index.

A typical Blueprint flow is:

`BeginPlay → Wait For Controller Changes → (On Connected) → Ensure Local Player For Controller → Restart Player`

Set the GameMode's `DefaultPawnClass` and place enough `PlayerStart` actors for the maximum number of local players. **Restart Player** then chooses a start, spawns the default Pawn and possesses it through Unreal's normal player lifecycle. Manual **Spawn Actor** + **Possess** is still valid when a game needs custom spawn rules, but it is not required for the common case.

Unreal limits a `UGameViewportClient` to four Local Players by default, even when splitscreen rendering is disabled. A game that needs more must raise the standard engine setting in its project `Config/DefaultEngine.ini` and provide the same number of `PlayerStart` actors:

```ini
[/Script/Engine.GameViewportClient]
MaxSplitscreenPlayers=6
```

The demo project used for the six-controller test is configured this way; the plugin does not change a project's player limit automatically.

Mapping contexts belong to the Local Player. The cleanest reusable place to add one is the custom PlayerController's `BeginPlay`:

`Self → Get Enhanced Input Local Player Subsystem → Add Mapping Context`

A standalone Joy-Con is presented in Nintendo's solo-horizontal grip: its stick is rotated into the
physical horizontal orientation and exposed as Unreal's standard left stick; its four front buttons become
positional face buttons; SL/SR become the standard left/right shoulders; and the inaccessible outer
L/ZL or R/ZR buttons do not generate gameplay input. When a left and right half are joined they return to
vertical presentation, with the left half supplying Unreal's left stick and the right half its right stick.
Direct `JSL4U` state getters retain the native hardware fields; only engine-facing gamepad keys are adapted.
In particular, the Joy-Con R's native inverted vertical stick axis is normalised before its horizontal or
joined presentation, and DualShock 4 right-stick Y receives its engine-facing correction without changing
the direct getter. Shared Enhanced Input modifiers such as **To World Space** therefore receive the same
local X/Y convention from Joy-Cons, DualShock/DualSense and Pro Controllers; no per-device modifier is
needed in the Mapping Context.
Therefore a normal movement Mapping Context should bind only `Gamepad Left 2D` — do not also bind
`Gamepad Right 2D` to movement, because that would make a full controller's camera stick and right-stick
drift move the Pawn.

If you instead do this from a Pawn's **Event Possessed**, pass `New Controller` straight into
**JSL4U Get Controllers Assigned To Player**, which accepts the generic `Controller` type. Do not use
**Get Player Controller 0** there: that always addresses player 0, including when the Pawn belongs to
another Local Player.

`Ensure Local Player For Controller` defaults `bCreateIfMissing` to true and is safe to call again. Guard any downstream game logic separately: for a standard GameMode, checking whether the returned PlayerController already possesses the expected Pawn is enough; for per-connection actors or UI, keep a `ConnectionId → Object` map. Also note that `bWasCreated` is **false for player 0**, because Unreal created that Local Player before `BeginPlay`; it does not mean that player 0 already has your gameplay Pawn.

> **Turn off "Skip Assigning Gamepad to Player 1" before you do.** It lives in *Project Settings → Maps & Modes → Local Multiplayer* and is **on by default**. While it is on, and only once a second local player exists, the engine shifts every gamepad's input to the *next* player — so the first controller starts driving player 2's pawn and the last controller drives a player that does not exist, silently. It is there for games where the keyboard is player 1 and gamepads start at player 2; it works against you otherwise.
>
> This one is worth knowing about because it is almost impossible to diagnose from the symptom. Everything you can inspect looks right — the plugin reports the correct player index, the platform input device mapper agrees with it, and each player controller possesses the pawn you expect. The redirect happens inside `UGameViewportClient::RemapControllerInput`, after all of that, so nothing upstream shows a discrepancy. The giveaway is that a single player works perfectly and the assignment only scrambles the moment the second player is created.

### Local multiplayer in the PIE viewport

PIE keeps Slate focus separately for every local user. After creating a second player, that user's focus can remain on the editor `SWindow` instead of the PIE `SViewport`; Slate then discards that user's input before it ever reaches the game viewport. Standalone and packaged games do not normally have this editor-window artifact.

For a playable PIE viewport, call Unreal's standard **Set Focus to Game Viewport** node after the local players, possession, and mapping contexts have been set up. That node focuses the game viewport for all Slate users. Call it again when closing an in-game menu if that menu intentionally moved focus. The plugin does not force focus automatically, because doing so would break legitimate UI input modes.

## Input events

For inputs that have an XInput equivalent (e.g. face buttons, triggers and sticks), simply adding JoyShockLibrary4Unreal to your project and enabling it will make Unreal recognize those inputs automatically for any compatible controller, with no code changes required.

For buttons that are exclusive to JoyShock inputs, new input events have been added:
![JoyShock inputs](https://github.com/user-attachments/assets/3ce0e091-e703-4781-823f-33e1aa615997)

The Switch 2 Pro Controller's exclusive buttons also have their own input events: **JoyShock C Button (Switch 2)**, **JoyShock Grip Left GL (Switch 2)** and **JoyShock Grip Right GR (Switch 2)**.

### Native input-device metadata

Every connection is registered with Unreal's `FInputDeviceRegistry`, and the plugin supplies the same `Config/Input.ini` hardware metadata pattern used by the engine's XInput and WinDualShock plugins. `UInputDeviceSubsystem` therefore sees these as standard **Gamepad** devices with stable identifiers:

- `DualShock4`, `DualSense`
- `JoyConLeft`, `JoyConRight`
- `SwitchProController`, `Switch2ProController`
- `JoyShockGamepad` as the unknown-device fallback

The identifier describes the model; `InputDeviceId` describes one physical connection. Do not parse the display name to identify hardware, and do not persist `InputDeviceId` or `ConnectionId` between game runs.

## Reacting to controllers

Controllers finish enumerating on a background thread, at a moment nothing in the game controls: in the
editor they are usually ready before the level loads, in a packaged game they usually are not, and a
Bluetooth controller may arrive minutes in. Code that only handles "already connected" works on the
machine it was written on and nowhere else.

The **Wait For…** nodes remove that distinction. Each is a single latent node whose event pins carry their
payload as ordinary data pins — no Create Event, no Bind Event, no matching custom-event signatures:

- **Wait For Controller Changes** — `On Connected` fires immediately for every controller already
  connected and then for each new one, so there is no window in which a controller can be missed;
  `On Disconnected` reports the last known identity and whether reports simply stopped.
- **Wait For Joy-Con Pairing Changes** — `On Joined` / `On Separated`, with both halves' identities
  already carrying their new grip mode, join partner and player assignment. Pairing actions are not
  replayed, because they are actions rather than persistent state; read **JSL4U Get Connected Controllers**
  when initial setup also needs the current pairing.
- **Wait For Battery Changes** — `On Low Battery` and `On Charging Changed`. See [Battery](#battery).
- **Wait For Controller Function Blocked** — see [Conflicts with other applications](#conflicts-with-other-applications).

Because `On Connected` already replays, do **not** add a separate `For Each` over
**JSL4U Get Connected Controllers** alongside it; doing both processes every existing controller twice.

Each node's `Async Action` output accepts **Cancel**, which is how a widget stops listening when it is
removed. `Remove From Parent` does not unbind anything on its own, so a widget that keeps listening after
being removed will keep running its handler against a half-dismantled state.

The same events are also available as assignable delegates on the **JoyShock Subsystem**, for C++ and for
Blueprints that prefer that style. If you use those directly, get the subsystem from a `BeginPlay` — not
from a Game Instance's `Init`, because subsystems are created at the very end of `Init` and the events
would silently never fire.

`FJSL4UControllerInfo` includes three different identities:

- `DeviceId` is the short JSL handle accepted by the other plugin nodes. JSL can reuse it after a disconnect.
- `ConnectionId` uniquely identifies this connection and is not reused during the run. Use it as the key of maps that track spawned Pawns or handled connections.
- `InputDeviceId` and `PlatformUserId` are Unreal's native identities, exposed as integers for Blueprint. `HardwareDeviceIdentifier` is the stable model name registered with the engine.

## Controller assignment and local multiplayer

Player slots are **stable**: a controller keeps its slot for as long as it stays connected, and a disconnect leaves that slot as a hole rather than shifting the others down — so if the player 1 controller drops mid-match, players 2 and 3 stay on their own characters instead of shuffling. The hole is reused by the next controller to connect. 4 solo-horizontal Joy-Cons = 4 players; two joined vertical pairs = 2 players. Joins dissolve automatically if one of the Joy-Cons disconnects.

Slots already taken by an **XInput pad** are skipped, so these controllers land where a second Xbox pad would have. Plug in an Xbox controller and a DualShock 4 and you get player 1 and player 2, the same as plugging in two Xbox controllers — a game does not need one assignment scheme for XInput and another for this plugin. (The flip side of matching XInput is that you inherit its behaviour: in a single-player game the second controller drives a player that does not exist, exactly as a second Xbox pad would.)

Slots otherwise follow the order controllers were switched on, and a controller that connected second stays on slot 1 even once it is the only one left — which in a single-player game means its input goes to a player that does not exist. Use **JSL4U Assign Controller To Player Index** to decide this yourself rather than inheriting connection order; it is the only thing that overrides it.

**Slots may be shared, and that is how any set of controllers drives one player.** Assign two, three or four controllers to the same slot and they all drive that player — a joined Joy-Con pair is simply the case the plugin performs for you, complete with the vertical stick/button split. Nothing is limited to Joy-Cons: a DualSense and a Pro Controller on one slot both move the same Pawn.

Assignment nodes are under **JoyShock Library | Controller Assignment**:

- **JSL4U Get Assigned Player Index** — the player slot a controller's input is delivered to.
- **JSL4U Assign Controller To Player Index (Device Id, Player Index)** — puts a controller on a chosen player slot. Pass -1 to hand it back to automatic assignment.
- **JSL4U Assign Controller To Player (Device Id, Player Controller)** — the same thing addressed by **PlayerController** instead of slot number.
- **JSL4U Get Controllers Assigned To Player** — every controller feeding a player, taking the generic `Controller` type so a Pawn's `Possessed` event plugs straight in. Two entries for a joined pair.
- **Ensure Local Player For Controller** (on the JoyShock Subsystem) — the high-level local-multiplayer path: reuse or create the correct Local Player from the controller's native Platform User, return its PlayerController, and reconcile the engine device mapper.

## Combining Joy-Cons into one player

A left+right Joy-Con pair can act as a single vertical controller for one player. Grip changes follow the
Switch convention globally:

- A Joy-Con connected by itself starts as a solo-horizontal controller.
- Press **SL + SR** on either half of a joined pair to separate both halves; each becomes horizontal.
- On two separated halves, hold **L or ZL** on the left and **R or ZR** on the right at the same time to
  join them as one vertical controller. For example, **ZL + ZR** is sufficient. The outer buttons are
  registration input and are suppressed until released, so the join does not also fire gameplay actions.
- When several left/right halves perform the chord in the same frame, device-id order pairs them
  deterministically.

Pairing nodes are under **JoyShock Library | Joy-Con Pairing**:

- **JSL4U Is Controller Type Joinable** — whether a controller type can be joined into a pair (currently the left and right Joy-Cons). `JSL4U Join Joy-Cons` validates with this same function.
- **JSL4U Join Joy-Cons (A, B)** — joins a left and a right Joy-Con so they feed a single player and sets both vertical (left half = left stick and its buttons, right half = right stick and its buttons).
- **JSL4U Unjoin Joy-Con / JSL4U Unjoin All Joy-Cons** — dissolve joins and return each half to horizontal presentation.
- **JSL4U Set Joy-Con Grip Mode** — explicit per-device override. Standalone defaults to Horizontal; set
  Vertical for exceptional games that intentionally use one upright half, such as Just Dance. Setting
  Horizontal on a joined half separates the pair.
- **JSL4U Get Joy-Con Partner** — whether this half is joined right now, and to which Device Id.
- **JSL4U Is Joy-Con Primary** — whether this device is the one representing its logical controller. True
  for any standalone controller of any type, and for exactly one half of a joined pair.
- **JSL4U Get Joy-Con Pair** — given either half, returns whether it is a pair plus both halves'
  identities. `Primary` names the same half no matter which one was asked, which is what lets an actor act
  on a pair without first working out which half it is attached to.

`Primary` identifies a *device*, not a *side*: which half leads depends on connection order, so a pair's
primary may be the right Joy-Con. Read `Controller Type` on the returned infos when something has to land
on a specific physical half.

## Motion and gyro

Motion is reported through **Unreal's own motion input**, the same path a phone's gyro and accelerometer use. That means `Tilt`, `RotationRate`, `Gravity` and `Acceleration` can be bound in Enhanced Input like any other axis, with no plugin-specific Blueprint code — `RotationRate` is the one you want for gyro aiming. The direct getters (`JSL4U Get IMU State`, `JSL4U Get Motion State`) are still there and use the same axes, so you can mix the two freely.

`JSL4U Get Motion State` returns a gravity-corrected orientation, so pitch and roll are absolute — they
stay anchored to real-world down rather than accumulating error. Yaw still drifts, as it must without a
magnetic or visual reference, which is what makes a "recentre" action worth offering in a game that maps
absolute orientation to anything.

**JSL4U Set Gyro Space** chooses the frame of reference gyro input is reported in — *Local Space* (raw,
relative to the controller), *World Space* (corrected by the measured gravity direction, so yaw is always
around the real vertical), or *Player Space* (a blend that is as adaptive as world space and as robust as
local space). Player Space is the usual choice for gyro aiming, and is worth exposing as a player
preference in a game that aims with gyro.

### Gyro calibration

A gyroscope reports a small non-zero rotation even when perfectly still, so a controller left alone slowly drifts. Calibrating measures that offset while the controller is still and subtracts it. Nodes live under **JoyShock Library | Gyro Calibration**.

**Most games need exactly one of them:** set **JSL4U Set Gyro Calibration Mode** to *Automatic* once per
controller and never touch the rest. Automatic works out for itself when the controller is being held
still and keeps the offset current.

The rest exist for games that want an explicit calibration screen:

- **JSL4U Start / Stop Manual Gyro Calibration** — gather samples for the drift offset while the controller sits still, then stop. Only meaningful in Manual mode.
- **JSL4U Reset Gyro Calibration** — throw the current offset away and start over. This is the one manual node worth exposing to players even in Automatic mode, because an offset measured while the controller was moving leaves the gyro worse off than no calibration at all, and this is the way out. It suits a "Recalibrate gyro" button.
- **JSL4U Get Gyro Calibration Status** — what drives a calibration screen's prompts and progress.
- **JSL4U Get / Set Gyro Calibration Offset** — read the offset as a **Vector** to save it per controller and restore it next session, so a returning player doesn't have to calibrate again. These use the same axes as *JSL4U Get IMU State*.

The status struct's three fields are easy to misread, so they are named after the mechanism they belong
to. **Auto Calibration Confidence** only ever climbs: it is a high-water mark meaning "this controller was
measured well at some point", not a live reading. **Auto Calibration Sampling Now** is the automatic
calibrator's own gate, true only while it is taking samples — it is *not* a general "the controller is
still" reading, and a controller lying untouched on a desk commonly reports false once the calibrator has
settled. **Manual Calibration Running** is the manual counterpart, true between Start and Stop. For a
genuine "is it moving right now" readout, compare the length of the gyro vector from *Get IMU State*
against a small threshold.

## Rumble

These controllers work with **Unreal's own force feedback**. `Play Force Feedback Effect`, `Client Play Force Feedback` and Enhanced Input's force feedback effects drive a DualShock 4, DualSense, Joy-Con or Pro Controller exactly as they drive an Xbox pad — so authored effects with curves, falloff and looping work, and you get one code path for every gamepad. Nothing to enable. Effects are aimed at a *player*, so both halves of a joined Joy-Con pair rumble together.

The channels map the way Unreal's XInput interface reads them: `LeftLarge` drives the heavy/low-frequency motor and `RightSmall` the light/high-frequency one, so an effect authored against a standard gamepad comes out the same here.

For direct control there is `JSL4U Set Rumble (DeviceId, SmallRumble, BigRumble)`, 0-1 per motor (call it with `(0, 0)` to stop). Both routes reach the same maximum — force feedback is clamped to 0-1 and 1 arrives as full strength — so an effect that feels weak is a weak curve in the asset rather than a limit here. (`Force Feedback Scale` and `b Force Feedback Enabled` on the PlayerController also apply.)

Use the direct node for the three things force feedback cannot do, all of them because it is aimed at a *player* rather than a controller: rumbling **one specific controller** (a joined Joy-Con pair is one player but two device ids, so only this can buzz just the left one); rumbling a controller **not assigned to any player**, which is what a controller-assignment screen needs for "press here and feel which controller this is" before players exist; and holding a constant intensity without authoring a looping asset.

The two are independent — a force feedback effect plays over a rumble you set directly, and neither cancels the other. Each motor runs at whichever of the two is stronger.

Per controller family:

- **DualShock 4 / DualSense**: small and big motor intensities.
- **Joy-Cons / Pro Controller (Switch 1)**: full HD-rumble amplitude control — `BigRumble` drives the low-frequency component (heavy shake), `SmallRumble` the high-frequency one (fine buzz). The vibration is sustained automatically until you set `(0, 0)`.
- **Switch 2 Pro Controller (USB)**: **no amplitude support.** While either value is above 0 the controller vibrates (sustained automatically, like the other controllers) until you call `(0, 0)`, so it is effectively on/off. The Switch 2's amplitude-accurate rumble channel hasn't been mapped over USB yet (Steam itself never rumbles this controller, so there was no traffic to reverse-engineer it from). Under the hood this retriggers the controller's short built-in vibration preset, so the texture may feel slightly pulsed compared to the Switch 1's HD rumble — and a force feedback effect that fades in or out will feel like a flat buzz on this controller.
- Note: close Steam when using the Switch 2 Pro Controller with Unreal — Steam holds the controller's USB command interface exclusively, which blocks the plugin's init and rumble (the plugin logs a warning and re-acquires the interface automatically once Steam is closed).

## Battery

`Controller Info` reports **Battery Level** and **Is Charging** for Joy-Cons, Switch Pro, DualShock 4 and
DualSense, plus **Battery Percent** where the hardware measures one. **Wait For Battery Changes** turns
those into events: `On Low Battery` fires once per episode and re-arms when the controller is charged or
plugged in, and `On Charging Changed` reports the cable or dock going in and out.

Battery Level is a coarse enum — `Empty`, `Critical`, `Low`, `Medium`, `Full` — deliberately, not a
percentage: a Switch controller reports five states, so a percentage there would be invented precision,
and a game written against a percentage misbehaves on exactly the controllers that cannot supply one.
Only the PlayStation controllers fill Battery Percent; it is `-1` elsewhere and is meant for display, not
for decisions. `Unknown` means the controller does not report charge (the Switch 2 Pro Controller), and is
never a low battery.

## Lights and player indicators

**Player indicators follow Unreal assignment automatically.** Joy-Cons, Switch Pro and Switch 2 Pro
Controllers use Nintendo's distinct player patterns (including combination patterns for players 5-8),
and joined Joy-Con halves show the same player. DualSense uses its five native player patterns. Calling
**JSL4U Set Player Indicator** manually overrides the displayed one-based number until the next controller
assignment change.

Nintendo's blue HOME-button light is a notification light, not a player number. Controllers can retain
that light from firmware or a previous host, so JSL4U clears it on Joy-Con R and Switch Pro after their
input stream starts, using Nintendo's explicit zero-brightness pattern; assignment uses only the four green
player LEDs. A game that wants that light as a feedback channel can take it with **JSL4U Set Home Light**,
after which the plugin stops clearing it on that controller for the rest of the connection. Before the game
or editor process opens, the plugin is not running and cannot change a controller's retained/default LED
state. Switch 2 Pro initialization likewise preserves its current indicator until Unreal assigns its actual
Local Player, instead of briefly turning every player LED off.

DualShock 4 has no numeric player LEDs, so **Set Player Indicator** intentionally does nothing on it. To
test its RGB light bar, call **JSL4U Set Light Color** with its `DeviceId` and an obvious color such as
magenta. The light bar and rumble are separate fields in the same output report, so a broken rumble motor
does not by itself prevent the color test.

## Conflicts with other applications

Steam Input, DS4Windows and similar tools can hold a controller's output path while input keeps flowing,
so rumble or player LEDs stop working with nothing to explain why. **Wait For Controller Function Blocked**
fires when an output write is rejected while the controller is still delivering input, naming which
capability is affected: `Rumble`, `Player Indicator`, `Home Light` or `Motion Sensor`. It fires once per
function until that function works again, and only when the game actually uses it.

Only failures the OS reports are detectable this way. An application that swallows output without failing
the write cannot be distinguished from one that is not running.

## Nintendo Switch 2 Pro Controller

The Switch 2 Pro Controller is supported over **USB** on Windows. Just plug it in — the plug-in initializes it through its WinUSB interface (the Switch 2 uses a new protocol that is not compatible with Switch 1 controllers), reads its factory stick calibration and colors, and parses all of its inputs:

- All buttons, including the new **C (GameChat)**, **GL** and **GR** buttons
- Both analog sticks, using the per-unit factory calibration
- Gyro and accelerometer, fully integrated with the motion API (`JSL4U Get Motion State`, `JSL4U Get And Clear Accumulated Gyro`, calibration, etc.)
- Player-indicator LEDs, kept in sync with the controller's assigned Unreal Local Player
- Multiple Switch 2 Pro Controllers at the same time

Bluetooth is currently **not** supported for the Switch 2 Pro Controller: it uses Bluetooth LE (GATT) and Windows does not expose it as a gamepad, which would require a dedicated BLE client. USB is the supported connection for now.

The Switch 2 command endpoint is WinUSB-exclusive even though its HID input endpoint is visible to more
than one process. This matters for Unreal's multi-process Standalone mode: the parent editor and the
Standalone game can discover the same controller. JSL4U releases an idle WinUSB command lease after one
second; a process that initially lost the race retries calibrated initialization after the lease becomes
available. Thus the playing process still reads the controller's factory stick centres and can use rumble
and LEDs without requiring Steam — or the parent editor — to be closed.

## Blueprint nodes

Blueprint exposes only the Unreal-oriented `JSL4U*` API. The old `Jsl*` nodes, their raw integer enums, loose float outputs and synchronous device scan have been removed. The low-level functions remain implementation details in C++ where the modern wrappers reuse them.

Current nodes are grouped by responsibility:

- **Events** — the latent **Wait For…** nodes.
- **Controllers** — discovery, connection checks and complete controller identity.
- **Controller Assignment** and **Local Multiplayer** — controller-to-player routing and native Local Player creation.
- **Joy-Con Pairing** — joining and separating Joy-Cons, and resolving a pair from either half.
- **Input State**, **Motion** and **Touchpad** — direct state queries for features not already better handled by Enhanced Input.
- **Gyro Calibration** — calibration mode, manual calibration, status and persistent offsets.
- **Output** — light color, player indicator, HOME light and direct-device rumble.
- **Diagnostics** — hardware resolution, poll interval and time since the last input report.

Node names and tooltips state units, invalid-result behavior and when Unreal's own Enhanced Input or Force Feedback should be preferred. Tooltips also say which node is the one most games need, and which exist only for less common cases. Search for `JSL4U` to list the complete callable API.

The old empty **Project Settings → Plugins → JoyShockLibrary4Unreal** page has been removed. It had no active setting and suggested configuration that the runtime never read; controller behavior is now either standard Unreal behavior or an explicit `JSL4U*` call.

JSL4U functions favour Unreal Engine's types and standards, so instead of returning three loose floats for an acceleration vector, they return an `FVector`. Vectors and quaternions use Unreal's left-handed, Z-up coordinate system rather than the original library's right-handed, Y-up axes.

## Diagnostics and troubleshooting

The temporary per-button dispatch and Slate-routing measurements used while developing the local
multiplayer example are not part of normal plugin output. For low-level HID troubleshooting,
`JoyShock.Debug.InputStalls 1` warns if a connected controller stops delivering reports for more than one
second and reports when delivery resumes. Set it back to `0` to disable it.

A single `Enabling IMU data...` line is expected while a controller is first initialized.

Bluetooth drops subcommands, and a lost one fails silently and permanently: the controller keeps streaming
buttons while its motion values stay at zero for the whole session. The Joy-Con handshake therefore waits
for the controller's acknowledgement of the IMU enable and the report-mode change, re-sending when it does
not arrive. Joy-Con firmware can also acknowledge the enable and still never start the sensor, so a stream
that stays motionless is repaired in place by toggling the IMU off and on — as a bare write, never a
subcommand exchange, because reading a reply from the polling thread would steal input reports. If both
fail, the plugin logs one warning and preserves the working button/stick stream rather than mutating it
further.

Configuration subcommands are otherwise never sent into an established Bluetooth stream. Re-asserting the
HOME light on a timer, for instance, caused right Joy-Cons to drop off Bluetooth after a few minutes, so
that light is cleared exactly once per connection.

Plugging a USB cable into a controller that is already paired over Bluetooth does not end the Bluetooth
link — it adds a second HID device. The plugin recognises both paths as one controller by its MAC address,
moves input onto the cable and back to the radio when it is pulled, and keeps the same device id, player
slot and Pawn throughout, so neither event reaches the game as a connect or disconnect.

## Questions

### Which versions of Unreal Engine has this been tested with?
It has been used in Unreal Engine 5.4 and, more recently, 5.8 (where it uses the new `FInputDeviceRegistry` API that replaces the deprecated `FInputDeviceScope`). I expect it to work in other versions of Unreal, but if you find any issues, feel free to let me know.

### Why should I use this plug-in instead of Steam Input or DS4Windows?

They each have their different use cases. Steam Input and DS4Windows do a fantastic job translating non-Xbox controller inputs into XInput, and I love that they can add gyro aiming to games that wouldn't support them otherwise by assigning gyro to Mouse. However, if you're making a game with JoyShockLibrary4Unreal, your game can support those controllers and make use of their features natively, without players having to worry about running background apps or remapping inputs.

### Can I use the controller motion processors contained in this plugin with official PlayStation and Switch controller libraries?
No official Sony or Nintendo libraries were used in the development or testing of JoyShockLibrary4Unreal, so I'm unable to answer that. However, you are welcome to modify the plug-in as you see fit. The functions in GamepadMotion.hpp should help you process motion data regardless of how you got it.

## Planned future updates

### Plugin
- **Bluetooth (BLE) support for the Switch 2 Pro Controller.** It currently works over USB only
- Amplitude-accurate rumble on the Switch 2 Pro Controller, so force feedback effects that fade in or out don't come out as a flat buzz on it. Needs its amplitude channel reverse-engineering over USB
- Check whether tracked controllers are released when a play session ends. `JslDisconnectAndDisposeAll` exists but may not be wired to the end of PIE, which would leave devices tracked between runs

### Hardware still to be tested
These are implemented but unverified, purely because no unit has been available. Reports from anyone who
owns one are very welcome.

- **Nintendo Switch 2 Joy-Con** — untested
- **Nintendo Switch Pro Controller (Switch 1)** — untested since the Unreal Engine 5.8 overhaul
- **Xbox and other XInput pads** — Unreal handles them directly, but the plugin skips player slots they already occupy so a JoyShock controller lands where a second Xbox pad would. That interaction needs a test with both kinds of controller connected at once
- **DualSense battery reporting** — the report offsets come from public reverse-engineering rather than measurement, and are marked as unverified in the code. The DualShock 4 equivalents were checked against DS4Windows

### Demo level
- **Controller models still missing from the on-screen mirror.** Only the Joy-Con pair, the Joy-Con grip, the DualShock 4 and the DualSense are modelled; everything else falls back to whatever the mirror is given. Needed: Nintendo Switch 2 Joy-Con, Nintendo Switch Pro Controller, Nintendo Switch 2 Pro Controller, and an Xbox pad for the controllers Unreal handles directly
- A controller-assignment screen demonstrating explicit reassignment and joined Joy-Cons
- Reconnection handling that deliberately transfers or retires the previous Pawn, depending on game policy
- Persisting gyro calibration between sessions with **Get / Set Gyro Calibration Offset** and a SaveGame, which the calibration panel currently only describes in text

## Credits
- A massive thanks to JibbSmart for creating the original JoyShockLibrary plug-in, and for answering the questions I sent to his Twitter DMs. For the full credits of the original JoyShockLibrary, check out his [JoyShockLibrary](https://github.com/JibbSmart/JoyShockLibrary) repo.
- microdee for the [HIDUE](https://github.com/microdee/HIDUE) Unreal plug-in, which JSL4U relies on for both USB and Bluetooth connections.
- Bundled DualSense 3D model created by [Saleem Akhtar](https://www.artstation.com/marketplace/p/zBM9R/ps5-duelsense-controller-3d-model-fbx).
- [uayten](https://github.com/uayten) for the 07/2026 overhaul: Unreal Engine 5.8 native input and local multiplayer; stable controller discovery, identity and player routing; Joy-Con horizontal play, joining/separation and player LEDs; total implementation of the Nintendo Switch 2 Pro Controller; battery reporting; cable/Bluetooth transport switching; the per-controller mirror, sensor HUD and demo workflow; consistent engine-facing stick axes; and the controller connection, freeze and shutdown-crash fixes.
