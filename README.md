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

`Jsl Set Rumble (DeviceId, SmallRumble, BigRumble)` sets the rumble motors (0-255 each; call it with `(0, 0)` to stop):

- **DualShock 4 / DualSense**: small and big motor intensities.
- **Joy-Cons / Pro Controller (Switch 1)**: full HD-rumble amplitude control — `BigRumble` drives the low-frequency component (heavy shake), `SmallRumble` the high-frequency one (fine buzz). The vibration is sustained automatically until you set `(0, 0)`.
- **Switch 2 Pro Controller (USB)**: simplified support — while either value is above 0 the controller vibrates (sustained automatically, like the other controllers) until you call `(0, 0)`. Intensity values are treated as on/off for now: the Switch 2's amplitude-accurate rumble channel hasn't been mapped over USB yet (Steam itself never rumbles this controller, so there was no traffic to reverse-engineer it from). Under the hood this retriggers the controller's short built-in vibration preset, so the texture may feel slightly pulsed compared to the Switch 1's HD rumble.
- Note: close Steam when using the Switch 2 Pro Controller with Unreal — Steam holds the controller's USB command interface exclusively, which blocks the plugin's init and rumble (the plugin logs a warning and re-acquires the interface automatically once Steam is closed).

## Combining Joy-Cons into one player

A left+right Joy-Con pair can act as a single controller for one player. New Blueprint nodes are available under **JoyShockLibrary | JoyConPairing**:

- **JSL4U Get Connected Controllers** — lists every connected controller as an **FJSL4UControllerInfo** (device id, type, player index, join partner, plus its live settings), e.g. to build a controller-assignment screen. Use *Enum to String* on `ControllerType` for a readable name.
- **JSL4U Is Joinable (Controller Type)** — whether a controller type can be joined into a pair (currently the left and right Joy-Cons). `JSL4U Join Joy Cons` validates with this same function.
- **JSL4U Join Joy Cons (A, B)** — joins a left and a right Joy-Con so they feed a single player (left half = left stick and its buttons, right half = right stick and its buttons). The engine sees one player per joined pair.
- **JSL4U Unjoin Joy Con / JSL4U Unjoin All Joy Cons** — dissolve joins (each Joy-Con becomes its own player again).
- **JSL4U Get Player Index** — the player slot a controller's input is delivered to.

Player slots are dense and stable: 4 solo Joy-Cons = 4 players; two joined pairs = 2 players. Joins dissolve automatically if one of the Joy-Cons disconnects.

## Blueprint nodes

All original JoyShockLibrary functions are still here and exposed to Blueprints. You can quickly find them by searching for the JSL prefix. 

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
- Assigning a controller to a specific player slot. Right now `PlayerIndex` is decided by connection order and is what actually routes the controller's input, so a game can't say "this controller is player 2" — a node to pin a controller to a chosen slot would allow that
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
