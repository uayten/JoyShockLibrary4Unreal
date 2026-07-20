# JoyShockLibrary4Unreal
This is a fork of JibbSmart's [JoyShockLibrary](https://github.com/JibbSmart/JoyShockLibrary), modified to integrate with Unreal Engine's input system as a plug-in. This allows your Unreal Engine games to support DualShock 4, DualSense (including Edge), Switch Pro, Joy-Con and Switch 2 Pro controllers natively, and use some of their exclusive features such as gyro and touchpad.

## Installation
- Download or clone the JoyShockLibrary4Unreal repo from this GitHub page and add it to your game's Plugins folder. The path to the Content folder should look like this: `<project>/Plugins/JoyShockLibrary4Unreal/Content`.
- Make sure that JoyShockLibrary4Unreal is enabled in your project's .uproject file or Plug-in settings.
- On Windows, no other plug-in is needed (hidapi is bundled). On other platforms, the plug-in falls back to microdee's [HIDUE](https://github.com/microdee/HIDUE) plugin for HID communication, so add it to your project as well.

## Demo Level
- There's a demo level called LV_JoyShockDemo in the Content folder! You can find it in your Content Browser, as long as you enable showing Plugin Content in your Content Browser settings:
![Accessing Test Level](https://github.com/user-attachments/assets/920f87a8-6de6-4efd-a3bd-b8787ea1a9d4)

Connect your controllers and press Play: the level spawns one visualiser per connected controller, side by side, each using the 3D model for its own type (Joy-Con L, Joy-Con R, Pro Controller or DualSense). Each model is tinted with the colour the controller itself reports, and mirrors its player-indicator LEDs, so the scene matches the hardware in your hands. Move a controller and you'll see its rotation in real time.

The HUD lists every connected controller; click a controller's name to switch the readout to that one. Alongside the identity it reports (device id, type, player index, join partner) you get its live motion values: IMU and quaternion orientation, acceleration and gravity.

The visualisers are spawned by BP_JoyShockInitializer, and BP_JoyShockVisualizer holds most of the per-controller logic, so both are worth a look. A controller will probably need calibration the first time you start it. I'll add a shortcut for this in a future update, but for now, select a spawned BP_JoyShockVisualizer in the Outliner while playing, go to the Details pane and click the button that says "Start Continuous Calibration" with the controller in a relaxed position, wait a couple of seconds, and then click on "Pause Continuous Calibration".


| Outliner  | Details pane |
| --------- | ------------ |
| ![image](https://github.com/user-attachments/assets/d6903620-1db0-41ff-b6a8-0bc605587bfb)  | ![image](https://github.com/user-attachments/assets/91c26248-ff39-44b4-914b-d810934e5a34) |


The level starts with the JoyShockVisualizer actor in Rotation mode, where it copies your controller's orientation, like in the video below.

https://github.com/user-attachments/assets/dfb27481-388e-4639-affd-2a887db05d33

In order to have this type of 1-to-1 rotation mapping in your game like in the video above, you should get the current Orientation from the JSL4U Get Motion State function:

![JSL4U Get Motion State](https://github.com/user-attachments/assets/5779b093-e757-466a-947e-c96adbd9a6b5)


Another mode you can enable is Aim Mode, by clicking the "Toggle Aim Mode" button in the Details pane (with the BP_JoyShockVisualizer actor selected, just like for calibration). This is a showcase of gyro aiming, and lets you move the controller on a 2D plane:

https://github.com/user-attachments/assets/de8e5e04-ed70-45ba-8188-43ad402b6117

In order to add Gyro aiming like this to your game, get the Y and Z values from the JSL4U Get And Flush Accumulated Gyro function, and apply those inputs to your player's aim:

![Get and flush Accumulated Gyro](https://github.com/user-attachments/assets/b032ec0e-9ebb-4d9c-bd2b-773eec098fcd)

## Input events

For inputs that have an XInput equivalent (e.g. face buttons, triggers and sticks), simply adding JoyShockLibrary4Unreal to your project (following the installation steps below) and enabling it will make Unreal recognize those inputs automatically for any compatible controller, with no code changes required.

For buttons that are exclusive to JoyShock inputs, new input events have been added:
![JoyShock inputs](https://github.com/user-attachments/assets/3ce0e091-e703-4781-823f-33e1aa615997)

The Switch 2 Pro Controller's exclusive buttons also have their own input events: **JoyShock C Button (Switch 2)**, **JoyShock Grip Left GL (Switch 2)** and **JoyShock Grip Right GR (Switch 2)**.

## Nintendo Switch 2 Pro Controller

The Switch 2 Pro Controller is supported over **USB** on Windows. Just plug it in — the plug-in initializes it through its WinUSB interface (the Switch 2 uses a new protocol that is not compatible with Switch 1 controllers), reads its factory stick calibration and colors, and parses all of its inputs:

- All buttons, including the new **C (GameChat)**, **GL** and **GR** buttons
- Both analog sticks, using the per-unit factory calibration
- Gyro and accelerometer, fully integrated with the motion API (`JSL4U Get Motion State`, `JSL4U Get And Flush Accumulated Gyro`, calibration, etc.)
- Multiple Switch 2 Pro Controllers at the same time

Bluetooth is currently **not** supported for the Switch 2 Pro Controller: it uses Bluetooth LE (GATT) and Windows does not expose it as a gamepad, which would require a dedicated BLE client. USB is the supported connection for now.

## Rumble

These controllers work with **Unreal's own force feedback**. `Play Force Feedback Effect`, `Client Play Force Feedback` and Enhanced Input's force feedback effects drive a DualShock 4, DualSense, Joy-Con or Pro Controller exactly as they drive an Xbox pad — so authored effects with curves, falloff and looping work, and you get one code path for every gamepad. Nothing to enable. Effects are aimed at a *player*, so both halves of a joined Joy-Con pair rumble together.

The channels map the way Unreal's XInput interface reads them: `LeftLarge` drives the heavy/low-frequency motor and `RightSmall` the light/high-frequency one, so an effect authored against a standard gamepad comes out the same here.

For direct control there is `JSL4U Set Rumble (DeviceId, SmallRumble, BigRumble)`, 0-1 per motor (call it with `(0, 0)` to stop). Use it to hold a constant intensity yourself, or to rumble one specific controller rather than "the player's" — a joined Joy-Con pair takes one force feedback effect but has two device ids. Force feedback and this node write the same two values, so whichever ran most recently wins.

Per controller family:

- **DualShock 4 / DualSense**: small and big motor intensities.
- **Joy-Cons / Pro Controller (Switch 1)**: full HD-rumble amplitude control — `BigRumble` drives the low-frequency component (heavy shake), `SmallRumble` the high-frequency one (fine buzz). The vibration is sustained automatically until you set `(0, 0)`.
- **Switch 2 Pro Controller (USB)**: **no amplitude support.** While either value is above 0 the controller vibrates (sustained automatically, like the other controllers) until you call `(0, 0)`, so it is effectively on/off. The Switch 2's amplitude-accurate rumble channel hasn't been mapped over USB yet (Steam itself never rumbles this controller, so there was no traffic to reverse-engineer it from). Under the hood this retriggers the controller's short built-in vibration preset, so the texture may feel slightly pulsed compared to the Switch 1's HD rumble — and a force feedback effect that fades in or out will feel like a flat buzz on this controller.
- Note: close Steam when using the Switch 2 Pro Controller with Unreal — Steam holds the controller's USB command interface exclusively, which blocks the plugin's init and rumble (the plugin logs a warning and re-acquires the interface automatically once Steam is closed).

## Combining Joy-Cons into one player

A left+right Joy-Con pair can act as a single controller for one player. New Blueprint nodes are available under **JoyShockLibrary | JoyConPairing**:

- **JSL4U Get Connected Controllers** — lists every connected controller as an **FJSL4UControllerInfo** (device id, type, player index, join partner, plus its live settings), e.g. to build a controller-assignment screen. Use *Enum to String* on `ControllerType` for a readable name.
- **JSL4U Is Joinable (Controller Type)** — whether a controller type can be joined into a pair (currently the left and right Joy-Cons). `JSL4U Join Joy Cons` validates with this same function.
- **JSL4U Join Joy Cons (A, B)** — joins a left and a right Joy-Con so they feed a single player (left half = left stick and its buttons, right half = right stick and its buttons). The engine sees one player per joined pair.
- **JSL4U Unjoin Joy Con / JSL4U Unjoin All Joy Cons** — dissolve joins (each Joy-Con becomes its own player again).
- **JSL4U Get Player Index** — the player slot a controller's input is delivered to.
- **JSL4U Set Player Index (Device Id, Player Index)** — puts a controller on a chosen player slot. Pass -1 to hand it back to automatic assignment.
- **JSL4U Set Controller For Player Controller (Device Id, Player Controller)** — the same thing addressed by **PlayerController** instead of slot number, and the setter counterpart of *JSL4U Get Controllers For Player Controller*.

Player slots are **stable**: a controller keeps its slot for as long as it stays connected, and a disconnect leaves that slot as a hole rather than shifting the others down — so if the player 1 controller drops mid-match, players 2 and 3 stay on their own characters instead of shuffling. The hole is reused by the next controller to connect. 4 solo Joy-Cons = 4 players; two joined pairs = 2 players. Joins dissolve automatically if one of the Joy-Cons disconnects.

The flip side is that slots otherwise follow the order controllers were switched on, and a controller that connected second stays on slot 1 even once it is the only one left — which in a single-player game means its input goes to a player that does not exist. Use **JSL4U Set Player Index** to decide this yourself rather than inheriting connection order; it is the only thing that overrides it. Slots may be shared: two controllers on one slot both drive that player, which is exactly what a joined Joy-Con pair is.

## Motion and gyro

Motion is reported through **Unreal's own motion input**, the same path a phone's gyro and accelerometer use. That means `Tilt`, `RotationRate`, `Gravity` and `Acceleration` can be bound in Enhanced Input like any other axis, with no plugin-specific Blueprint code — `RotationRate` is the one you want for gyro aiming. The direct getters (`JSL4U Get IMU State`, `JSL4U Get Motion State`) are still there and use the same axes, so you can mix the two freely.

## Gyro calibration

A gyroscope reports a small non-zero rotation even when perfectly still, so a controller left alone slowly drifts. Calibrating measures that offset while the controller is still and subtracts it. Nodes live under **JoyShockLibrary | Gyro**:

- **JSL4U Set Gyro Calibration Mode (Device Id, Mode)** — *Automatic* lets the controller work out for itself when it is being held still and keep itself calibrated; *Manual* leaves it entirely to the nodes below. **Most games only need this one, set to Automatic once per controller.**
- **JSL4U Start / Stop Gyro Calibration** — gather samples for the drift offset while the controller sits still, then stop. For driving an explicit "hold still while we calibrate" step in an options screen. Only meaningful in Manual mode.
- **JSL4U Reset Gyro Calibration** — throw the current offset away and start over, for when a calibration was taken while the controller was actually being moved (which leaves the gyro worse off than no calibration at all).
- **JSL4U Get Gyro Calibration Status** — confidence, whether the controller currently reads as steady, the current mode, and whether a manual calibration is in progress. This is what drives a calibration screen's prompts and progress.
- **JSL4U Get / Set Gyro Calibration Offset** — read the offset as a **Vector** to save it per controller and restore it next session, so a returning player doesn't have to calibrate again. These use the same axes as *JSL4U Get IMU State*; don't mix them with the legacy `JslGet/SetCalibrationOffset`, which report the same offset in the library's own axes.

Separately, **JSL4U Set Gyro Space** chooses the frame of reference gyro input is reported in — *Local Space* (raw, relative to the controller), *World Space* (corrected by the calculated gravity direction, so yaw is always around the real vertical), or *Player Space* (a blend that is as adaptive as world space and as robust as local space). Player Space is the usual choice for gyro aiming.

## Blueprint nodes

All original JoyShockLibrary functions are still here and exposed to Blueprints. You can quickly find them by searching for the JSL prefix.

Legacy `Jsl*` nodes that have a `JSL4U*` equivalent are marked **deprecated**: they still work, but the editor now warns and names the node to move to. The JSL4U versions speak Unreal's types — Vectors and Vector2Ds instead of loose floats, enums instead of magic integers, Colors, and 0-1 ranges — and the warnings exist so the legacy layer can eventually be deleted without hunting for call sites. Nodes with no JSL4U counterpart yet (`Jsl Connect Devices`, `Jsl Still Connected`, `Jsl Get Touchpad Dimension`, `Jsl Get Poll Rate`, and the analog resolution getters) are not marked.


![JSL Functions](https://github.com/user-attachments/assets/08010581-64ec-45c8-9e79-9fc8a2315349)

Additionally, new functions have been created and exposed to Blueprints with the JSL4U prefix:

![JSL4U Functions](https://github.com/user-attachments/assets/1d59f8cd-2888-4ab3-b1ca-2a29b00f3f6b)

JSL4U functions favour Unreal Engine's types and standards, so instead of returning, for example, 3 floats for an acceleration vector, they return an FVector. Additionally, vectors and quaternions were updated to be in Left-handed Z-up, as opposed to Right-handed Y-up.

## Which versions of Unreal Engine has this been tested with?
It has been used in Unreal Engine 5.4 and, more recently, 5.8 (where it uses the new `FInputDeviceRegistry` API that replaces the deprecated `FInputDeviceScope`). I expect it to work in other versions of Unreal, but if you find any issues, feel free to let me know.

## Why should I use this plug-in instead of Steam Input or DS4Windows?

They each have their different use cases. Steam Input and DS4Windows do a fantastic job translating non-Xbox controller inputs into XInput, and I love that they can add gyro aiming to games that wouldn't support them otherwise by assigning gyro to Mouse. However, if you're making a game with JoyShockLibrary4Unreal, your game can support those controllers and make use of their features natively, without players having to worry about running background apps or remapping inputs. 

## Can I use the controller motion processors contained in this plugin with official PlayStation and Switch controller libraries?
No official Sony or Nintendo libraries were used in the development or testing of JoyShockLibrary4Unreal, so I'm unable to answer that. However, you are welcome to modify the plug-in as you see fit. The functions in GamepadMotion.hpp should help you process motion data regardless of how you got it.

## Planned future updates

### Plugin
- Improved multiplayer support, especially when mixed with XInput controllers
- Amplitude-accurate rumble on the Switch 2 Pro Controller, so force feedback effects that fade in or out don't come out as a flat buzz on it. Needs its amplitude channel reverse-engineering over USB
- Generalising Joy-Con joining, so that any set of controllers can drive a single player rather than only a left + right Joy-Con pair
- Player-indicator LEDs on the Switch 2 Pro Controller. It doesn't speak the Switch 1 subcommand that sets them, so unlike every other supported controller its lights stay off; setting them needs the command to be reverse-engineered from its own protocol
- Bluetooth (BLE) support for the Switch 2 Pro Controller
- Amplitude-accurate rumble for the Switch 2 Pro Controller (requires mapping its dedicated vibration channel over USB)
- Check whether tracked controllers are released when a play session ends. `JslDisconnectAndDisposeAll` exists but may not be wired to the end of PIE, which would leave devices tracked between runs

### Demo level
- Easier calibration, and demonstrating more features such as Touch
- Joy-Con LED materials that light up and turn off to match the physical controller, driven by the reported player LED
- A Joy-Con strap accessory on the Joy-Con models
- A Grip model with both Joy-Cons seated in it
- An example playable pawn, controllable by one or more controllers at the same time
- HUD buttons to create a playable pawn, delete it, and hand it over to another controller

## Credits
- A massive thanks to JibbSmart for creating the original JoyShockLibrary plug-in, and for answering the questions I sent to his Twitter DMs. For the full credits of the original JoyShockLibrary, check out his [JoyShockLibrary](https://github.com/JibbSmart/JoyShockLibrary) repo.
- microdee for the [HIDUE](https://github.com/microdee/HIDUE) Unreal plug-in, which JSL4U relies on for both USB and Bluetooth connections.
- DualSense 3D model in the test level created by [Saleem Akhtar](https://www.artstation.com/marketplace/p/zBM9R/ps5-duelsense-controller-3d-model-fbx).
- [uayten](https://github.com/uayten) for adding Nintendo Switch 2 Pro Controller (USB) support with rumble, Joy-Con combined/separated pairing with dedicated Blueprint nodes, and fixing editor freezes when controllers connect/disconnect.
