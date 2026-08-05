// JoyShockLibrary.h - Contains declarations of functions
#pragma once
#include <shared_mutex>
#include "Containers/StaticArray.h"
#include "JoyShockLibrary.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogJoyShockLibrary, Verbose, All);

class AController;
class APlayerController;

// A physical output capability the plugin drives on a controller. Used by the blocked-function event to
// name which capability another application appears to be interfering with. Values are also bit indices
// into JoyShock::failed_output_functions, so the order is load-bearing.
UENUM(BlueprintType, meta = (DisplayName = "JSL4U Controller Function"))
enum class EJSL4UControllerFunction : uint8
{
	Rumble UMETA(ToolTip = "Vibration output (direct and sustained rumble)."),
	PlayerIndicator UMETA(ToolTip = "Player LEDs / light-bar player identification."),
	HomeLight UMETA(ToolTip = "The Switch HOME notification light."),
	MotionSensor UMETA(ToolTip = "IMU configuration commands (enabling or repairing gyro and accelerometer).")
};

/*#if _MSC_VER // this is defined when compiling with Visual Studio
#define JOY_SHOCK_API __declspec(dllexport) // Visual Studio needs annotating exported functions with this
#else
#define JOY_SHOCK_API // XCode does not need annotating exported functions, so define is empty
#endif*/

#define JS_TYPE_JOYCON_LEFT 1
#define JS_TYPE_JOYCON_RIGHT 2
#define JS_TYPE_PRO_CONTROLLER 3
#define JS_TYPE_DS4 4
#define JS_TYPE_DS 5
#define JS_TYPE_PRO_CONTROLLER_2 6
#define JS_TYPE_JOYCON2_LEFT 7
#define JS_TYPE_JOYCON2_RIGHT 8

#define JS_SPLIT_TYPE_LEFT 1
#define JS_SPLIT_TYPE_RIGHT 2
#define JS_SPLIT_TYPE_FULL 3

#define JSMASK_UP 0x000001
#define JSMASK_DOWN 0x000002
#define JSMASK_LEFT 0x000004
#define JSMASK_RIGHT 0x000008
#define JSMASK_PLUS 0x000010
#define JSMASK_OPTIONS 0x000010
#define JSMASK_MINUS 0x000020
#define JSMASK_SHARE 0x000020
#define JSMASK_LCLICK 0x000040
#define JSMASK_RCLICK 0x000080
#define JSMASK_L 0x000100
#define JSMASK_R 0x000200
#define JSMASK_ZL 0x000400
#define JSMASK_ZR 0x000800
#define JSMASK_S 0x001000
#define JSMASK_E 0x002000
#define JSMASK_W 0x004000
#define JSMASK_N 0x008000
#define JSMASK_HOME 0x010000
#define JSMASK_PS 0x010000
#define JSMASK_CAPTURE 0x020000
#define JSMASK_TOUCHPAD_CLICK 0x020000
#define JSMASK_MIC 0x040000
#define JSMASK_SL 0x080000
#define JSMASK_SR 0x100000
#define JSMASK_FNL 0x200000
#define JSMASK_FNR 0x400000
// Nintendo Switch 2 Pro Controller extras
#define JSMASK_C 0x800000    // "C" (GameChat) button
#define JSMASK_GL 0x1000000  // rear grip button GL
#define JSMASK_GR 0x2000000  // rear grip button GR

#define JSOFFSET_UP 0
#define JSOFFSET_DOWN 1
#define JSOFFSET_LEFT 2
#define JSOFFSET_RIGHT 3
#define JSOFFSET_PLUS 4
#define JSOFFSET_OPTIONS 4
#define JSOFFSET_MINUS 5
#define JSOFFSET_SHARE 5
#define JSOFFSET_LCLICK 6
#define JSOFFSET_RCLICK 7
#define JSOFFSET_L 8
#define JSOFFSET_R 9
#define JSOFFSET_ZL 10
#define JSOFFSET_ZR 11
#define JSOFFSET_S 12
#define JSOFFSET_E 13
#define JSOFFSET_W 14
#define JSOFFSET_N 15
#define JSOFFSET_HOME 16
#define JSOFFSET_PS 16
#define JSOFFSET_CAPTURE 17
#define JSOFFSET_TOUCHPAD_CLICK 17
#define JSOFFSET_MIC 18
#define JSOFFSET_SL 19
#define JSOFFSET_SR 20
#define JSOFFSET_FNL 21
#define JSOFFSET_FNR 22
#define JSOFFSET_C 23
#define JSOFFSET_GL 24
#define JSOFFSET_GR 25

// PS5 Player maps for the DS Player Lightbar
#define DS5_PLAYER_1 4
#define DS5_PLAYER_2 10
#define DS5_PLAYER_3 21
#define DS5_PLAYER_4 27
#define DS5_PLAYER_5 31

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJoyShockState // typedef struct JOY_SHOCK_STATE
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	int32 buttons = 0;
	
	UPROPERTY(BlueprintReadOnly)
	float lTrigger = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float rTrigger = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float stickLX = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float stickLY = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float stickRX = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float stickRY = 0.f;

	void Update(const FJoyShockState& UpdateStateData, const FJoyShockState& PreviousTickState)
	{
		buttons = PreviousTickState.buttons ^ ((PreviousTickState.buttons ^ buttons) | (PreviousTickState.buttons ^ UpdateStateData.buttons));
		lTrigger = UpdateStateData.lTrigger;
		rTrigger = UpdateStateData.rTrigger;
		stickLX = UpdateStateData.stickLX;
		stickLY = UpdateStateData.stickLY;
		stickRX = UpdateStateData.stickRX;
		stickRY = UpdateStateData.stickRY;
	}
}; // JOY_SHOCK_STATE;

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4UJoyShockState // typedef struct JOY_SHOCK_STATE
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	int32 Buttons = 0;
	
	UPROPERTY(BlueprintReadOnly)
	float LeftTrigger = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float RightTrigger = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	FVector2D LeftStick = FVector2D::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	FVector2D RightStick = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FIMUState // typedef struct IMU_STATE
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	float accelX = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float accelY = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float accelZ = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float gyroX = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float gyroY = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float gyroZ = 0.f;
}; // IMU_STATE;

// New Struct
USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4UIMUState
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector Gyro = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FMotionState // typedef struct MOTION_STATE
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float quatW = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float quatX = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float quatY = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float quatZ = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float rawQuatW = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float rawQuatX = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float rawQuatY = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float rawQuatZ = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float accelX = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float accelY = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float accelZ = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float gravX = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float gravY = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	float gravZ = 0.f;
}; // MOTION_STATE;

// New Struct
USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4UMotionState
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	FQuat Orientation = FQuat::Identity;

	UPROPERTY(BlueprintReadOnly)
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector Gravity = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FTouchState // typedef struct TOUCH_STATE {
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	int32 t0Id = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 t1Id = 0;

	UPROPERTY(BlueprintReadOnly)
	bool t0Down = false;

	UPROPERTY(BlueprintReadOnly)
	bool t1Down = false;

	UPROPERTY(BlueprintReadOnly)
	float t0X = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float t0Y = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float t1X = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float t1Y = 0.f;
}; // TOUCH_STATE;

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4USingleTouchState
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	int32 TouchID = 0;

	UPROPERTY(BlueprintReadOnly)
	bool bIsDown = false;
	
	UPROPERTY(BlueprintReadOnly)
	FVector2D Location = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4UTouchState
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	FJSL4USingleTouchState PrimaryTouch = {};
	
	UPROPERTY(BlueprintReadOnly)
	FJSL4USingleTouchState SecondaryTouch = {};
};

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSLAutoCalibration // typedef struct JSL_AUTO_CALIBRATION {
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadOnly)
	float confidence = 0.f;
	
	UPROPERTY(BlueprintReadOnly)
	bool autoCalibrationEnabled = false;
	
	UPROPERTY(BlueprintReadOnly)
	bool isSteady = false;
}; // JSL_AUTO_CALIBRATION;

/**
 * Which model a controller is. Ordered by family rather than by history, so a Switch node reads the way a
 * controller-select screen does; the numbers behind the names are not meaningful and nothing outside this
 * enum depends on them.
 */
UENUM(BlueprintType)
enum class EJSL4UControllerType : uint8
{
	Undefined = 0 UMETA(DisplayName = "Undefined"),
	// Any controller Unreal accepts that this plugin does not drive. On Windows that means XInput, which is
	// Xbox pads and the very many third-party controllers presenting as one. Named after the API rather
	// than the brand because the API is the part that is actually true: a controller arriving this way is
	// certainly XInput, and only probably an Xbox. (Strictly the value means "not ours", so a device from
	// some other input plugin would land here too -- put a friendlier label on it in your UI if you want
	// one, rather than making the enum claim more than it knows.) Only Wait For Any Controller Changes ever
	// reports it; the JSL4U-specific nodes never do. Distinct from Undefined, which means a controller this
	// plugin IS driving but could not identify.
	XInputController = 1 UMETA(DisplayName = "XInput Controller"),
	DualShock4 = 2 UMETA(DisplayName = "DualShock 4"),
	DualSense = 3 UMETA(DisplayName = "DualSense"),
	ProController = 4 UMETA(DisplayName = "Pro Controller"),
	ProController2 = 5 UMETA(DisplayName = "Pro Controller 2"),
	JoyConLeft = 6 UMETA(DisplayName = "Joy-Con (L)"),
	JoyConRight = 7 UMETA(DisplayName = "Joy-Con (R)"),
	// Bluetooth only. A Joy-Con 2 never appears over USB -- the console charges it through the rails, and a
	// cable to a PC gives nothing -- so it arrives from the radio scan, and only once it has been paired
	// (hold SYNC) or woken with a button press. Joins into a pair exactly as a Switch 1 Joy-Con does.
	JoyCon2Left = 8 UMETA(DisplayName = "Joy-Con 2 (L)"),
	// Bluetooth only -- see Joy-Con 2 (L) above.
	JoyCon2Right = 9 UMETA(DisplayName = "Joy-Con 2 (R)")
};

/**
 * A controller's charge, in the coarse steps the hardware actually reports.
 *
 * Deliberately not a percentage: a Switch controller reports five states in its input report, so a
 * percentage would be invented precision. Sony controllers report finer steps, which are bucketed into
 * the same five so a game can treat every controller the same way.
 *
 * Unknown is not "flat" -- it means this controller does not report charge (the Switch 2 Pro Controller,
 * currently), or none has arrived yet. Compare against it explicitly before treating a value as low.
 */
UENUM(BlueprintType)
enum class EJSL4UBatteryLevel : uint8
{
	Unknown = 0 UMETA(ToolTip = "This controller does not report its charge, or no report has arrived yet."),
	Empty = 1 UMETA(ToolTip = "Effectively flat; shutdown is imminent."),
	Critical = 2 UMETA(ToolTip = "Very low."),
	Low = 3 UMETA(ToolTip = "Low -- the level Wait For Battery Changes warns at by default."),
	Medium = 4 UMETA(ToolTip = "Comfortably charged."),
	Full = 5 UMETA(ToolTip = "Full or nearly full.")
};

UENUM(BlueprintType)
enum class EJSL4UJoyConGripMode : uint8
{
	NotApplicable = 0 UMETA(DisplayName = "Not a Joy-Con"),
	Vertical = 1 UMETA(DisplayName = "Vertical"),
	Horizontal = 2 UMETA(DisplayName = "Horizontal")
};

UENUM(BlueprintType)
enum class EJSL4UGyroSpace : uint8
{
	LocalSpace = 0 UMETA(DisplayName = "Local Space"), // no transformation is done on gyro input
	WorldSpace = 1 UMETA(DisplayName = "World Space"), // gyro input is transformed based on the calculated gravity direction to account for the player's preferred controller orientation
	PlayerSpace = 2 UMETA(DisplayName = "Player Space"), // a simple combination of local and world space that is as adaptive as world space but is as robust as local space
};

/**
 * How a controller's gyro drift offset is arrived at.
 *
 * A gyroscope reports a small non-zero rotation even when perfectly still, and that offset drifts with
 * temperature and age. Calibrating means measuring it while the controller is still and subtracting it.
 */
UENUM(BlueprintType)
enum class EJSL4UGyroCalibrationMode : uint8
{
	// The controller works out for itself when it is being held still and calibrates continuously, with no
	// action from the player. This is what most games want -- set it once and never think about it again.
	Automatic UMETA(DisplayName = "Automatic"),
	// Calibration only happens between JSL4UStartManualGyroCalibration and JSL4UStopManualGyroCalibration. Use this to
	// drive an explicit "put the controller down and hold still" step in an options screen.
	Manual UMETA(DisplayName = "Manual"),
};

/** A controller's current gyro calibration state, for showing progress in a calibration screen. */
USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4UGyroCalibrationStatus
{
	GENERATED_BODY()

	// How sure the automatic calibration is of its current offset, 0 to 1. Meaningless in Manual mode.
	// Note that this only ever climbs: it is a high-water mark, not a live reading, so 100% means "this
	// controller was measured well at some point", not "it is being measured well right now".
	UPROPERTY(BlueprintReadOnly, meta = (DisplayName = "Auto Calibration Confidence"))
	float Confidence = 0.f;

	/**
	 * Whether the automatic calibrator is currently accepting this controller as still enough to sample
	 * from. It is the calibrator's own gate, not a general "is the controller moving" reading, and the
	 * difference bites: the stillness test compares against a noise floor that only ever ratchets DOWN and
	 * stops adapting once Confidence reaches 1, with barely any headroom. A controller lying untouched on
	 * a desk can therefore read false indefinitely -- it merely rests slightly noisier than the quietest
	 * moment ever measured -- while Confidence stays pinned at 100%, since Confidence never decays either.
	 * Reset Gyro Calibration clears both and lets it settle again.
	 *
	 * So this is a useful cue inside a calibration flow ("keep holding" versus "sampling"), and the wrong
	 * thing to label "controller is still" on a HUD. For that, compare the length of the gyro vector from
	 * Get IMU State against a small threshold -- that is a genuine reading of the present moment.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (DisplayName = "Auto Calibration Sampling Now"))
	bool bIsSteady = false;

	// The mode this controller is in (see JSL4USetGyroCalibrationMode).
	UPROPERTY(BlueprintReadOnly)
	EJSL4UGyroCalibrationMode Mode = EJSL4UGyroCalibrationMode::Manual;

	// Whether a manual calibration is currently gathering samples, i.e. JSL4UStartManualGyroCalibration was
	// called and JSL4UStopManualGyroCalibration has not been. The automatic counterpart is the flag above; the
	// two never run at once, since automatic sampling is suspended while a manual calibration is active.
	UPROPERTY(BlueprintReadOnly, meta = (DisplayName = "Manual Calibration Running"))
	bool bIsCalibrating = false;
};

// Everything the plugin knows about a controller: its identity, its type, the player slot it feeds and
// its live JSL settings. Returned for a single device by JSL4UGetControllerInfo and for every
// connected device by JSL4UGetConnectedControllers.
USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4UControllerInfo // typedef struct JSL_SETTINGS {
{
	GENERATED_BODY()

	/**
	 * True when this plugin is the one driving the controller, and so when everything below the identity
	 * block means anything.
	 *
	 * Every JSL4U node that returns controllers returns only ours, so this is true throughout -- except
	 * from Get All Connected Controllers and Wait For Any Controller Changes, which report every controller
	 * Unreal accepts so that "has a player joined" can be answered once for all of them. A controller that
	 * is not ours fills in only what Unreal knows about any device -- Connection Id, Player Index, Input
	 * Device Id, Hardware Device Identifier -- and reports the capabilities below honestly: false for the
	 * hardware it does not have, true for the rumble every pad has, which reaches it through Unreal's own
	 * force feedback.
	 *
	 * This is not a test you need before calling a node. Every node takes a Connection Id and answers for
	 * any controller, serving what it can and doing nothing (with one log warning) for what it cannot. Ask
	 * the capability flag for the thing you are about to use -- Has Motion Sensors before reading a gyro --
	 * rather than asking whose controller it is.
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsJoyShockController = false;

	// The controller's identity, and the only address the plugin's nodes take. Unique for this connection:
	// never reused when a controller disconnects and another takes its place. Use it as the key of
	// persistent assignment maps.
	//
	// Positive (from 1) for a controller this plugin drives. A controller it does not drive gets a negative
	// one instead, so the two sources can key the same map without ever colliding, and so the sign alone
	// says which kind of controller a node was handed.
	//
	// The two halves differ in one way worth knowing, now that this is the only address there is: a
	// positive id is unique for the whole run, while a negative one is derived from Unreal's device id and
	// comes back into circulation once that pad leaves. Both are safe to hold -- a stale id of either kind
	// resolves to nothing while the pad is gone -- but only the positive kind is safe to hold ACROSS a
	// reconnect. Neither survives a session on disk.
	//
	// Zero is never issued: it is what this field reads before a controller has an identity at all, and no
	// node hands out a controller in that state (see JSL4UGetConnectedControllers). A 0 arriving in a
	// Blueprint is an uninitialised struct, not a controller.
	UPROPERTY(BlueprintReadOnly)
	int64 ConnectionId = 0;

	// Unreal's native identities for this connection. These are exposed as integers because
	// FInputDeviceId / FPlatformUserId are not Blueprint structs.
	UPROPERTY(BlueprintReadOnly)
	int32 InputDeviceId = -1;

	UPROPERTY(BlueprintReadOnly)
	int32 PlatformUserId = -1;

	// Stable identifier registered with UInputDeviceSubsystem (for example DualSense or JoyConLeft).
	UPROPERTY(BlueprintReadOnly)
	FName HardwareDeviceIdentifier = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	EJSL4UControllerType ControllerType = EJSL4UControllerType::Undefined;

	// The player slot (0, 1, 2, ...) this controller's input is delivered to, or -1 if it isn't connected.
	// Both halves of a joined Joy-Con pair share the same PlayerIndex.
	UPROPERTY(BlueprintReadOnly)
	int32 PlayerIndex = -1;

	// If this controller is joined with another, the connection id of its partner; otherwise 0.
	UPROPERTY(BlueprintReadOnly)
	int64 JoinedToConnectionId = 0;

	// Standalone Joy-Cons default to Horizontal. Joining a left and right half makes both Vertical.
	// Non-Joy-Con controllers report NotApplicable.
	UPROPERTY(BlueprintReadOnly)
	EJSL4UJoyConGripMode JoyConGripMode = EJSL4UJoyConGripMode::NotApplicable;

	// The number shown on the controller's player indicator (the Switch or DualSense player LEDs), as set
	// by JSL4USetPlayerIndicator. DualShock 4 has an RGB light bar but no numeric player indicator. This is
	// a display value, not an identity -- to identify a controller use ConnectionId, and to know which player it
	// feeds use PlayerIndex.
	UPROPERTY(BlueprintReadOnly)
	int32 PlayerLedNumber = 0;

	// Charge level, in the coarse steps the hardware reports. Unknown when the controller does not report
	// it. Sony values still need verification against real hardware -- see the parsing in InputHelpers.cpp.
	UPROPERTY(BlueprintReadOnly)
	EJSL4UBatteryLevel BatteryLevel = EJSL4UBatteryLevel::Unknown;

	// Whether the controller is drawing power (cable, dock, or a Joy-Con seated in a charging grip).
	UPROPERTY(BlueprintReadOnly)
	bool bIsCharging = false;

	// Charge from 0 to 100, or -1 when the controller does not report one finely enough to quote. Only the
	// PlayStation controllers fill this. Prefer Battery Level for anything that decides something: it is
	// the only reading every family can give, so a game written against it behaves the same on a Joy-Con
	// as on a DualSense. This is for display -- a percentage on a settings screen, a battery bar.
	UPROPERTY(BlueprintReadOnly)
	int32 BatteryPercent = -1;

	UPROPERTY(BlueprintReadOnly)
	FColor Color = FColor::Black;

	// Output/input capabilities used by diagnostic UI. Prefer these to switches on ControllerType so new
	// controller variants can inherit the right UI without every Blueprint being updated.
	UPROPERTY(BlueprintReadOnly)
	bool bHasRgbLight = false;

	UPROPERTY(BlueprintReadOnly)
	bool bHasPlayerIndicator = false;

	UPROPERTY(BlueprintReadOnly)
	bool bHasMotionSensors = false;

	UPROPERTY(BlueprintReadOnly)
	bool bHasTouchpad = false;

	UPROPERTY(BlueprintReadOnly)
	bool bHasRumble = false;

	// The space this controller's gyro is currently reported in, as set by JSL4USetGyroSpace.
	UPROPERTY(BlueprintReadOnly)
	EJSL4UGyroSpace GyroSpace = EJSL4UGyroSpace::LocalSpace;

	// (JSL's raw SplitType used to sit here. It said nothing ControllerType does not already say --
	// left half, right half or whole controller -- as an unlabelled integer, so it was dropped rather
	// than promoted to an enum that would have duplicated EJSL4UControllerType.)

	UPROPERTY(BlueprintReadOnly)
	bool bIsCalibrating = false;

	UPROPERTY(BlueprintReadOnly)
	bool bAutoCalibrationEnabled = false;

	UPROPERTY(BlueprintReadOnly)
	bool bIsConnected = false;
};

USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSLSettings // typedef struct JSL_SETTINGS {
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 gyroSpace = 0;

	// UPROPERTY(BlueprintReadOnly)
	uint32 colour = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 playerNumber = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 controllerType = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 splitType = 0;

	UPROPERTY(BlueprintReadOnly)
	bool isCalibrating = false;

	UPROPERTY(BlueprintReadOnly)
	bool autoCalibrationEnabled = false;

	UPROPERTY(BlueprintReadOnly)
	bool isConnected = false;
}; // JSL_SETTINGS;

// Stops every controller's polling thread and destroys the devices. Called by the module on its way out,
// which is the only place it belongs: a game has no reason to tear the library down while it is running.
// Returns false if a thread was still running when the wait ran out, in which case its device is left alive
// on purpose and the hidapi library must not be unloaded.
bool JslShutdownAllDevices();

// The sensors that only the Switch 2 controllers carry. These are not motion -- the accelerometer and
// gyroscope are read through the usual IMU nodes, which every driven controller answers -- and nothing else
// in the plugin's families reports them, which is why they are gathered here instead of being added to the
// IMU state that a DualSense would then have to answer with zeroes.
//
// Any other controller returns this struct with Is Supported false and every field zero, so a Blueprint can
// read it unconditionally and branch on the flag.
USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4USwitch2Sensors
{
	GENERATED_BODY()

	// False for every controller that is not a Switch 2, and for a Switch 2 that has not reported yet.
	UPROPERTY(BlueprintReadOnly)
	bool bIsSupported = false;

	// The battery cell's terminal voltage. Roughly 4.2 full and 3.0 empty, but it sags under load and
	// recovers at rest -- Battery Level on the controller info is the interpolated version of this, and the
	// better thing to drive a UI from. This is here for a readout that wants the measurement itself.
	UPROPERTY(BlueprintReadOnly)
	float BatteryVolts = 0.f;

	// Treat as unconfirmed. A pair on the same desk at the same room temperature read 25 and 36, and the 25
	// is exactly the constant this is offset from -- so that half's raw field was zero, and the offset is a
	// guess that has never been checked against a thermometer. Fine as a curiosity, not as a measurement.
	UPROPERTY(BlueprintReadOnly)
	float TemperatureCelsius = 0.f;

	// Raw magnetometer counts. Uncalibrated: a magnetometer reads the local field, which in a room means
	// the building's steel and whatever is on the desk as much as it means north. Useful as an extra axis
	// for a fusion filter that knows how to calibrate it, not as a compass on its own.
	UPROPERTY(BlueprintReadOnly)
	FVector Magnetometer = FVector::ZeroVector;

	// The optical sensor in the controller's underside, the one the console uses for mouse mode.
	//
	// An absolute position, not a per-frame delta: it runs from 0 at the top-left to 65535 and then wraps
	// back to 0, on both axes, and it is always a whole number despite the float. Which means a delta is a
	// subtraction that has to survive that wrap -- plain "now minus last" reports a jump of ±65536 every
	// time an axis rolls over. Measured on hardware at roughly 12000 units per length of a Pro Controller,
	// with no acceleration curve of the sensor's own.
	UPROPERTY(BlueprintReadOnly)
	FVector2D MousePosition = FVector2D::ZeroVector;

	// The sensor's read of the surface under it. Distance rises as the controller is lifted, so these are
	// what tell you whether it is actually being used as a mouse rather than held in the air.
	//
	// Measured on a Joy-Con 2: roughness is about 4600 in the air, 4380 on a mousepad, 2500 on a bare desk
	// and 2000 on cloth. Distance sits near 3000 in the air, starts falling about 5 mm from a surface, and
	// rests at 140-150 against one -- a threshold on it is a fair way to decide the controller is being
	// used as a mouse at all.
	UPROPERTY(BlueprintReadOnly)
	int32 MouseRoughness = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 MouseDistance = 0;
};

UCLASS()
class JOYSHOCKLIBRARY4UNREAL_API UJoyShockLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Returns every currently connected controller, in connection order. Use this to list controllers
	// (e.g. "JoyCon (R), DualSense, JoyCon (L)") and pick which Joy-Cons to join. Use
	// "Enum to String" on ControllerType for a readable name.
	//
	// "Connected" means the controller has an engine identity, not merely that the library has it open: one
	// whose connect has not reached the game thread yet is left out for those few frames rather than listed
	// with the Connection Id and Input Device Id it does not have yet. It arrives on Wait For Controller
	// Changes the moment it does, and the two never disagree about what it is.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Controllers",
		meta = (DisplayName = "JSL4U Get Connected Controllers", ToolTip = "Returns every connected JoyShock controller with its plugin and Unreal device identities, model, assignment and current settings."))
	static TArray<FJSL4UControllerInfo> JSL4UGetConnectedControllers();

	/**
	 * Returns every controller Unreal accepts, ours and everyone else's, as one list.
	 *
	 * The polling counterpart to Wait For Any Controller Changes, and it exists because that node used to
	 * be the only way to see a controller this plugin does not drive. A game that wanted the current roster
	 * at any other moment -- opening a pause menu, rebuilding a player-select screen -- got a list with the
	 * Xbox pads missing and had to keep its own tally from the event to fill the gap. This is that tally,
	 * asked for directly.
	 *
	 * Ours come first, in connection order, so the JSL4U block of the list matches Get Connected
	 * Controllers exactly; the rest follow in the order Unreal reports them. The keyboard and mouse are
	 * left out, for the same reason the event node leaves them out: they are connected from the first frame
	 * and are not a player joining.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Controllers",
		meta = (DisplayName = "JSL4U Get All Connected Controllers", Keywords = "xinput xbox any every gamepad roster list joyshock", ToolTip = "Returns every controller Unreal accepts -- this plugin's and the XInput pads it does not drive -- in one list. Every node here takes a Connection Id and accepts either kind; check the capability flags to know what a given controller can actually answer."))
	static TArray<FJSL4UControllerInfo> JSL4UGetAllConnectedControllers();

	/**
	 * Describes a controller this plugin does not drive, from what Unreal knows about any input device.
	 *
	 * C++ only: FInputDeviceId and FPlatformUserId are not Blueprint types, which is the whole reason the
	 * identities in FJSL4UControllerInfo are integers. Shared so the polling query and the event node
	 * cannot drift into describing the same pad two different ways.
	 */
	static FJSL4UControllerInfo JSL4UDescribeUndrivenDevice(FPlatformUserId PlatformUser,
		FInputDeviceId InputDevice);

	// True for controller types that can be joined into a pair -- currently the left and right Joy-Cons.
	// This is the single source of truth for "can this be joined": JSL4UJoinJoyCons validates with it too.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Joy-Con Pairing",
		meta = (DisplayName = "JSL4U Is Controller Type Joinable", ToolTip = "Returns true when this controller type can be paired with another controller. Currently this means left and right Joy-Cons."))
	static bool JSL4UIsControllerTypeJoinable(EJSL4UControllerType ControllerType);

	// Joins two Joy-Cons so they act as a single controller for one player: their inputs are merged and
	// delivered to the player of whichever half connected first. Both must be Joy-Cons (one left, one
	// right). Returns true on success.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Joy-Con Pairing",
		meta = (DisplayName = "JSL4U Join Joy-Cons", ToolTip = "Pairs one left and one right Joy-Con so both feed the same player. Returns false when either device is unavailable or the types are incompatible."))
	static bool JSL4UJoinJoyCons(int64 ConnectionIdA, int64 ConnectionIdB);

	// Undoes a join involving this controller (both halves go back to being their own players).
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Joy-Con Pairing",
		meta = (DisplayName = "JSL4U Unjoin Joy-Con", ToolTip = "Dissolves the Joy-Con pair containing this device. Both halves return to independent assignment."))
	static void JSL4UUnjoinJoyCon(int64 ConnectionId);

	// Undoes every Joy-Con join.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Joy-Con Pairing",
		meta = (DisplayName = "JSL4U Unjoin All Joy-Cons", ToolTip = "Dissolves every joined Joy-Con pair."))
	static void JSL4UUnjoinAllJoyCons();

	// Overrides one Joy-Con's grip presentation. Horizontal rotates its stick/buttons into standard
	// one-gamepad positions and separates it from a joined pair. Vertical is intended for exceptional
	// single-Joy-Con games such as Just Dance.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Joy-Con Pairing",
		meta = (DisplayName = "JSL4U Set Joy-Con Grip Mode", ToolTip = "Sets one Joy-Con to Horizontal or Vertical presentation. Horizontal separates a joined pair. Standalone Joy-Cons use Horizontal by default."))
	static bool JSL4USetJoyConGripMode(int64 ConnectionId, EJSL4UJoyConGripMode GripMode);

	/**
	 * Whether this Joy-Con is currently joined with another, and to which one -- read live rather than
	 * from a struct.
	 *
	 * Prefer this over a stored Controller Info's JoinedToConnectionId whenever the answer is needed at an
	 * arbitrary moment (drawing a mirror, refreshing UI). A Controller Info is a snapshot: one captured
	 * when a controller connected still says "not joined" forever, because joining happens later, and
	 * nothing rewrites the copy the caller kept. This asks the input interface for the current pairing.
	 */
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Joy-Con Pairing",
		meta = (DisplayName = "JSL4U Get Joy-Con Partner", Keywords = "join joined pair partner",
			ToolTip = "Returns whether this Joy-Con is joined right now, and its partner's Connection Id (0 when standalone). Reads live pairing state, unlike a stored Controller Info."))
	static bool JSL4UGetJoyConPartner(int64 ConnectionId, int64& PartnerConnectionId);

	/**
	 * Whether this device is the one that represents its logical controller: true for any standalone
	 * controller, and for exactly one half of a joined pair.
	 *
	 * This is the plugin's own grouping rule (the same one player slots are assigned from), so it answers
	 * "should this actor draw the pair, or stand down?" without the caller inventing a tie-break that
	 * could drift from the plugin's. Note it identifies a device, not a side: which half wins depends on
	 * connection order, so a pair's primary may be the right Joy-Con. When the answer must always be the
	 * same physical half (drawing a grip that holds both), branch on Controller Type instead.
	 */
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Joy-Con Pairing",
		meta = (DisplayName = "JSL4U Is Joy-Con Primary", Keywords = "join joined pair primary leader",
			ToolTip = "True when this device represents its logical controller: always true for a standalone controller of any type, and true for exactly one half of a joined Joy-Con pair. Use it to decide which of two paired actors is the active one."))
	static bool JSL4UIsJoyConPrimary(int64 ConnectionId);

	/**
	 * Resolves a whole logical controller from either of its halves, in a single call: whether it is
	 * a joined pair, which half leads it, and both halves' full identities.
	 *
	 * Ask it with either half and the answer is the same -- Primary is always the device the plugin uses
	 * to represent the pair (the same one Is Join Primary reports and player slots are assigned from), and
	 * Partner is always the other. That stability is the point: an actor drawing a pair does not have to
	 * work out which of the two it happens to be attached to.
	 *
	 * Standalone controllers are not a special case to handle separately: Primary is that controller,
	 * Partner comes back unset (its Is Connected is false), and the return value is false. So this is also
	 * a safe first call at start-up, where controllers are already connected and no pairing event will
	 * ever arrive to announce them.
	 *
	 * Primary is a device, not a side: which half leads depends on connection order, so a pair's Primary
	 * may be the right Joy-Con. When something has to go on a specific half -- painting the left Joy-Con's
	 * colour on the left half of a mesh -- read Controller Type on each returned info rather than assuming.
	 */
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Joy-Con Pairing",
		meta = (DisplayName = "JSL4U Get Joy-Con Pair", ToolTip = "Given either half, returns whether it is a joined pair plus both controller infos (Primary leads, Partner is the other half). A standalone controller returns itself as Primary, an unset Partner and false. Reads live state, so it works for controllers that were already connected at start-up."))
	static bool JSL4UGetJoyConPair(int64 ConnectionId, FJSL4UControllerInfo& PrimaryController,
		FJSL4UControllerInfo& PartnerController);

	/**
	 * How many local players this game may create -- i.e. the ceiling on Create Player, and so on how many
	 * characters a game can spawn for the controllers this plugin reports.
	 *
	 * Unreal keeps this number on the game viewport under the name MaxSplitscreenPlayers, which is a
	 * misnomer worth knowing about: it caps Create Player whether or not the screen is ever split, and it
	 * is still enforced with Force Disable Split Screen on. It defaults to 4, which is why a fifth
	 * controller can connect, be reported here, be given player slot 4 -- and still get no character.
	 * Splitscreen layouts stop at 4 because four is as many views as fit on a television; that is a
	 * rendering limit and has nothing to do with how many people are playing, which is the only thing this
	 * number really means.
	 *
	 * The alternative is an entry in DefaultEngine.ini under [/Script/Engine.GameViewportClient], which
	 * puts a decision belonging to your game in a config file, under the wrong name, where nobody looking
	 * at your player-spawning code will find it. Prefer setting it here, from wherever you decide how many
	 * players your game supports.
	 *
	 * This does not spawn or remove anything by itself, and lowering it does not evict players who already
	 * exist. Set it before you create players -- Begin Play is the natural place.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Controller Assignment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "JSL4U Set Max Local Players", ToolTip = "Sets how many local players this game may create, overriding the engine's default of 4. Call it before creating players."))
	static bool JSL4USetMaxLocalPlayers(const UObject* WorldContextObject, int32 MaxLocalPlayers);

	// The current ceiling on Create Player -- see JSL4USetMaxLocalPlayers. Returns -1 when there is no game
	// viewport to ask (a Blueprint running outside a game world).
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Controller Assignment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "JSL4U Get Max Local Players", ToolTip = "Returns how many local players this game may create, or -1 when there is no game viewport."))
	static int32 JSL4UGetMaxLocalPlayers(const UObject* WorldContextObject);

	/**
	 * Assigns a controller to a player slot, overriding the slot it was given when it connected.
	 *
	 * Slots are otherwise decided by connection order and are stable: a controller keeps its slot until it
	 * disconnects, and its slot is then left as a hole rather than shifting the others down, so nobody
	 * swaps characters mid-game. The consequence is that the slots you end up with depend on the order
	 * controllers were switched on -- if the player 1 controller disconnects, the remaining ones do NOT
	 * move down into slot 0. This is the node that fixes that, and it is the only thing that decides slots
	 * other than connection order.
	 *
	 * Slots may be shared: assigning two controllers to one slot makes both drive that player, which is
	 * exactly what a joined Joy-Con pair already does. Assigning either half of a joined pair moves the
	 * pair. The assignment lasts as long as the controller stays connected -- on reconnect it is a new
	 * controller and gets a slot automatically again.
	 *
	 * Takes the whole controller description so it can move an XInput pad too: one this plugin drives goes
	 * through the plugin's own slot table, one it does not is remapped through Unreal's input device
	 * mapper. Both end up on the same slot, and a game does not need to know which kind it is holding.
	 *
	 * @param Controller   The controller to assign (from Get All Connected Controllers, or the payload of
	 *                     any of the Wait For ... Changes nodes).
	 * @param PlayerIndex  The player slot to put it on, counting from 0. Pass -1 to hand the controller
	 *                     back to automatic assignment -- for a pad this plugin does not drive that is the
	 *                     unpaired user, which is Unreal's own way of saying the same thing.
	 * @return False if the controller is not connected.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Controller Assignment",
		meta = (DisplayName = "JSL4U Assign Controller To Player Index", ToolTip = "Assigns this controller to a zero-based player slot. Works for XInput pads too. Pass -1 to restore automatic assignment. Joined Joy-Cons move together."))
	static bool JSL4UAssignControllerToPlayerIndex(const FJSL4UControllerInfo& Controller, int32 PlayerIndex);

	// Assigns a controller to the player behind a PlayerController -- the setter counterpart of
	// JSL4UGetControllersAssignedToPlayer, and the one-node answer to "make this controller drive this
	// player". Same caveat as the getter: do NOT build this out of "Get Player Controller ID", which is the
	// legacy controller id rather than the platform user index slots are assigned from.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Controller Assignment",
		meta = (DisplayName = "JSL4U Assign Controller To Player", DefaultToSelf = "PlayerController", ToolTip = "Assigns this controller to the Local Player owned by Player Controller. Works for XInput pads too. Defaults to Self in a PlayerController Blueprint."))
	static bool JSL4UAssignControllerToPlayer(const FJSL4UControllerInfo& Controller, APlayerController* PlayerController);

	// The inverse of the PlayerIndex a controller reports in its FJSL4UControllerInfo: every controller
	// currently feeding a player slot. Two entries for a joined Joy-Con pair (rumble both to rumble "the
	// player"), one for a standalone controller, none if nothing is assigned to that slot. PlayerIndex is a
	// platform user index -- if you have a PlayerController, prefer JSL4UGetControllersAssignedToPlayer,
	// which converts it for you.
	// Answers for every controller Unreal accepts, XInput pads included, so "does this player have a
	// controller" is one question rather than one per controller family. Check the capability
	// flags on a result before calling a controller-specific node with it.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Controller Assignment",
		meta = (DisplayName = "JSL4U Get Controllers Assigned To Player Index", ToolTip = "Returns every controller feeding this zero-based player slot, XInput pads included. A joined Joy-Con pair returns both halves."))
	static TArray<FJSL4UControllerInfo> JSL4UGetControllersAssignedToPlayerIndex(int32 PlayerIndex);

	// The controller(s) of the player behind a Controller -- i.e. of whoever issued the command you
	// are reacting to. Defaults to self inside a Controller Blueprint, so this is the one-node answer
	// to "which controller is this player holding?" (e.g. to rumble it).
	// Takes the base AController so the reference a Pawn's Possessed event (or Get Controller) hands out
	// plugs in directly -- the PlayerController downcast happens here, natively. An AIController (or a
	// PlayerController with no Local Player yet, e.g. a remote net client) owns no physical input devices
	// and returns an empty array.
	// Note: do NOT build this out of "Get Player Controller ID". That is the legacy controller id, which is
	// a different number from the platform user index that player slots are assigned from -- this converts
	// through the same IPlatformInputDeviceMapper the assignment uses.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Controller Assignment",
		meta = (DefaultToSelf = "Controller", DisplayName = "JSL4U Get Controllers Assigned To Player", ToolTip = "Returns every controller feeding the Local Player behind this Controller, XInput pads included. Accepts the Controller from a Pawn's Possessed event directly; AI controllers return an empty array. Defaults to Self in a Controller Blueprint."))
	static TArray<FJSL4UControllerInfo> JSL4UGetControllersAssignedToPlayer(AController* Controller);

	/**
	 * Asks the plugin to re-scan for controllers, on a background thread.
	 *
	 * You rarely need this: controllers are picked up automatically at startup and whenever Windows reports a
	 * device change. It exists for the cases that produce no device-change message -- and it returns
	 * immediately, with the scan happening off the game thread, so it is safe to call from gameplay.
	 * Repeated calls while a scan is running are coalesced into one follow-up pass.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Controllers",
		meta = (DisplayName = "JSL4U Refresh Controllers", ToolTip = "Requests an asynchronous controller rescan. Normal device changes are detected automatically, so this is only needed when the platform sends no notification."))
	static void JSL4URefreshControllers();

	static int32 JslConnectDevices();

	/**
	 * Fills OutDeviceHandleArray with the device id of every connected controller and returns how many
	 * there were. Kept as a low-level C++ helper; Blueprint uses JSL4UGetConnectedControllers.
	 * "Connected" means the controller has actually delivered input, not merely that it turned up in HID
	 * enumeration -- a controller that has just been switched off can linger in enumeration for a moment,
	 * and is deliberately not listed (nor reported as connected) during that window.
	 * For new Blueprints, prefer JSL4UGetConnectedControllers: it names each controller by Connection Id
	 * plus each controller's type, player slot and settings, so you rarely need this raw handle list.
	 */
	static int32 JslGetConnectedDeviceHandles(/* int* */ TArray<int32>& OutDeviceHandleArray); //, int32 InSize);

	/**
	 * Whether this connection id currently refers to a connected, working controller.
	 *
	 * Agrees with JSL4UGetConnectedControllers: a device that turned up in enumeration but has not delivered
	 * input (a controller that has just been switched off can linger there for a moment) reports false here
	 * too, unlike the lower-level HID enumeration check.
	 *
	 * A stored id whose controller has been unplugged reports false and keeps reporting false: connection
	 * ids are not reused, so this can never quietly become true again for a different controller. (The one
	 * asterisk is a pad this plugin does not drive: its negative id is derived from Unreal's device id,
	 * which does come back into circulation.) Prefer reacting to the JoyShock subsystem's connect/disconnect
	 * events over polling this; it is meant for cheaply validating an id you are already holding on to,
	 * without building the whole controller list to look it up.
	 */
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Controllers",
		meta = (DisplayName = "JSL4U Is Controller Connected", ToolTip = "Returns true only when Connection Id currently identifies a connected controller. Answers for every controller Unreal accepts, not only this plugin's."))
	static bool JSL4UIsControllerConnected(int64 ConnectionId);

	static bool JslStillConnected(int32 deviceId);

	// get buttons as bits in the following order, using North South East West to name face buttons to avoid ambiguity between Xbox and Nintendo layouts:
	// 0x00001: up
	// 0x00002: down
	// 0x00004: left
	// 0x00008: right
	// 0x00010: plus
	// 0x00020: minus
	// 0x00040: left stick click
	// 0x00080: right stick click
	// 0x00100: L
	// 0x00200: R
	// ZL and ZR are reported as analogue inputs (GetLeftTrigger, GetRightTrigger), because DS4 and XBox controllers use analogue triggers, but we also have them as raw buttons
	// 0x00400: ZL
	// 0x00800: ZR
	// 0x01000: S
	// 0x02000: E
	// 0x04000: W
	// 0x08000: N
	// 0x10000: home / PS
	// 0x20000: capture / touchpad-click
	// 0x40000: SL
	// 0x80000: SR
	// These are the best way to get all the buttons/triggers/sticks, gyro/accelerometer (IMU), orientation/acceleration/gravity (Motion), or touchpad
	static FJoyShockState JslGetSimpleState(int32 deviceId);

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Input State",
		meta = (DisplayName = "JSL4U Get Controller State", ToolTip = "Returns one controller's current buttons, sticks and triggers in Unreal-friendly types. Reads that device directly, so it answers before the controller belongs to any player -- which is what a controller-assignment screen needs (\"press a button on the pad you want to be player 2\"). For gameplay, bind Enhanced Input instead: it is per-player, and these same inputs already reach it."))
	static FJSL4UJoyShockState JSL4UGetControllerState(int64 ConnectionId);
	
	static FIMUState JslGetIMUState(int32 deviceId);

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Motion",
		meta = (DisplayName = "JSL4U Get IMU State", ToolTip = "Returns one controller's gyroscope and acceleration in Unreal axes, after applying the selected gyro space. Reads that device directly, so it answers before the controller belongs to any player -- for assignment screens, calibration UI and diagnostics. For gameplay, this plugin already reports motion to Unreal's motion input, so bind Tilt / Rotation Rate / Gravity / Acceleration in Enhanced Input instead. Returns zeroes for a controller without motion sensors -- check Has Motion Sensors on the controller info."))
	static FJSL4UIMUState JSL4UGetIMUState(int64 ConnectionId);

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Motion",
		meta = (DisplayName = "JSL4U Get Raw IMU State", ToolTip = "Returns one controller's untransformed gyroscope and acceleration in Unreal axes, ignoring the selected gyro space. Use Get IMU State unless you specifically need the untransformed reading."))
	static FJSL4UIMUState JSL4UGetRawIMUState(int64 ConnectionId);
	
	static FMotionState JslGetMotionState(int32 deviceId);

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Motion",
		meta = (DisplayName = "JSL4U Get Motion State", ToolTip = "Returns one controller's processed orientation, acceleration and gravity in Unreal coordinates. Reads that device directly, so it answers before the controller belongs to any player. For gameplay, bind Enhanced Input's motion inputs instead. Returns zeroes for a controller without motion sensors -- check Has Motion Sensors on the controller info."))
	static FJSL4UMotionState JSL4UGetMotionState(int64 ConnectionId);

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Motion",
		meta = (DisplayName = "JSL4U Get Raw Motion State", ToolTip = "Returns one controller's untransformed orientation, acceleration and gravity from the motion processor. Use Get Motion State unless you specifically need the untransformed reading."))
	static FJSL4UMotionState JSL4UGetRawMotionState(int64 ConnectionId);

	static FTouchState JslGetTouchState(int32 deviceId, bool previous = false);

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Touchpad",
		meta = (DisplayName = "JSL4U Get Touch State", ToolTip = "Returns one controller's two touch contacts. Enable Previous to read the preceding report instead of the current one. Reads that device directly, so it answers before the controller belongs to any player. For gameplay, this plugin already reports the touchpad as gamepad axes, so bind TouchPad 1 / TouchPad 2 in Enhanced Input instead. Returns nothing touched for a controller without a touchpad -- check Has Touchpad on the controller info."))
	static FJSL4UTouchState JSL4UGetTouchState(int64 ConnectionId, bool bPrevious = false);

	// The touchpad's size in its own units (1920 x 943 on the DualShock 4 and DualSense), or zero for a
	// controller without one. JSL4UGetTouchState reports touches normalised to 0-1, so multiply by this if
	// you need touchpad-native coordinates -- e.g. to keep a drag's aspect ratio right.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Touchpad",
		meta = (DisplayName = "JSL4U Get Touchpad Size", ToolTip = "Returns the touchpad's native width and height, or zero for a controller without a touchpad. Touch State positions are normalized from 0 to 1."))
	static FVector2D JSL4UGetTouchpadSize(int64 ConnectionId);

	static bool JslGetTouchpadDimension(int32 deviceId, int32 &sizeX, int32 &sizeY);

	static int32 JslGetButtons(int32 deviceId);

	// get thumbsticks

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Input State",
		meta = (DisplayName = "JSL4U Get Left Stick", ToolTip = "Returns one controller's current left-stick position, from -1 to 1. Reads that device directly, so it answers before the controller belongs to any player -- for assignment screens and diagnostics. For gameplay, bind Enhanced Input instead."))
	static FVector2D JSL4UGetLeftStick(int64 ConnectionId);
	
	static float JslGetLeftX(int32 deviceId);

	static float JslGetLeftY(int32 deviceId);

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Input State",
		meta = (DisplayName = "JSL4U Get Right Stick", ToolTip = "Returns one controller's current right-stick position, from -1 to 1. Reads that device directly, so it answers before the controller belongs to any player -- for assignment screens and diagnostics. For gameplay, bind Enhanced Input instead."))
	static FVector2D JSL4UGetRightStick(int64 ConnectionId);

	static float JslGetRightX(int32 deviceId);

	static float JslGetRightY(int32 deviceId);

	// get triggers. Switch controllers don't have analogue triggers, but will report 0.0 or 1.0 so they can be used in the same way as others
	static float JslGetLeftTrigger(int32 deviceId);

	static float JslGetRightTrigger(int32 deviceId);

	// get gyro
	static float JslGetGyroX(int32 deviceId);

	static float JslGetGyroY(int32 deviceId);

	static float JslGetGyroZ(int32 deviceId);

	// get accumulated average gyro since this function was last called or last flushed values
	static void JslGetAndFlushAccumulatedGyro(int32 deviceId, float& gyroX, float& gyroY, float& gyroZ);

	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Motion",
		meta = (DisplayName = "JSL4U Get And Clear Accumulated Gyro", ToolTip = "Returns the accumulated gyro rotation since the previous call, then clears the accumulator. Values use Unreal axes."))
	static FVector JSL4UGetAndClearAccumulatedGyro(int64 ConnectionId);

	// set gyro space. JslGetGyro*, JslGetAndFlushAccumulatedGyro, JslGetIMUState, and the IMU_STATEs reported in the callback functions will use one of 3 transformations:
	// 0 = local space -> no transformation is done on gyro input
	// 1 = world space -> gyro input is transformed based on the calculated gravity direction to account for the player's preferred controller orientation
	// 2 = player space -> a simple combination of local and world space that is as adaptive as world space but is as robust as local space
	static void JslSetGyroSpace(int32 deviceId, int32 gyroSpace);

	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Motion",
		meta = (DisplayName = "JSL4U Set Gyro Space", ToolTip = "TYPICAL USE: Player Space, the usual choice for gyro aiming -- as adaptive as world space and as robust as local space. Local Space is the untransformed controller frame; World Space corrects by the measured gravity direction so yaw is always around the real vertical. Worth exposing as a player preference in a game that aims with gyro."))
	static void JSL4USetGyroSpace(int64 ConnectionId, EJSL4UGyroSpace GyroSpace);

	// get accelerometer
	static float JslGetAccelX(int32 deviceId);

	static float JslGetAccelY(int32 deviceId);

	static float JslGetAccelZ(int32 deviceId);

	// get touchpad
	static int32 JslGetTouchId(int32 deviceId, bool secondTouch = false);

	static bool JslGetTouchDown(int32 deviceId, bool secondTouch = false);

	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Touchpad",
		meta = (DisplayName = "JSL4U Get Touch Position", ToolTip = "Returns the selected touch contact's normalized position from 0 to 1. Use Get Touch State when you also need contact id or down state."))
	static FVector2D JSL4UGetTouchPosition(int64 ConnectionId, bool bSecondTouch = false);

	static float JslGetTouchX(int32 deviceId, bool secondTouch = false);

	static float JslGetTouchY(int32 deviceId, bool secondTouch = false);

	// analog parameters have different resolutions depending on device
	// The smallest change this controller can report on a stick axis. Sticks are 8-bit on a DualShock 4 and
	// 12-bit on Switch controllers, so this differs per device -- useful for sizing a deadzone or an
	// on-screen readout to what the hardware can actually resolve.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Diagnostics",
		meta = (DisplayName = "JSL4U Get Stick Resolution Step", ToolTip = "Returns the smallest change this controller can report on one stick axis."))
	static float JSL4UGetStickResolutionStep(int64 ConnectionId);

	// The smallest change this controller can report on a trigger. Switch controllers have no analog
	// triggers and report 1 (fully on or fully off).
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Diagnostics",
		meta = (DisplayName = "JSL4U Get Trigger Resolution Step", ToolTip = "Returns the smallest trigger change this controller can report. Digital Switch triggers return 1."))
	static float JSL4UGetTriggerResolutionStep(int64 ConnectionId);

	// How often this controller sends input reports, in milliseconds per report.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Diagnostics",
		meta = (DisplayName = "JSL4U Get Poll Interval", ToolTip = "Returns this controller's expected milliseconds between input reports. This is an interval, not reports per second."))
	static float JSL4UGetPollInterval(int64 ConnectionId);

	// Seconds since this controller last sent an input report. A value climbing well past the poll rate means
	// the controller has gone quiet, which is the difference between the engine not routing its input and the
	// controller not sending any.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Diagnostics",
		meta = (DisplayName = "JSL4U Get Seconds Since Last Report", ToolTip = "Returns seconds since the controller last delivered an input report. A steadily increasing value indicates the device has stopped reporting."))
	static float JSL4UGetSecondsSinceLastReport(int64 ConnectionId);

	static float JslGetStickStep(int32 deviceId);

	static float JslGetTriggerStep(int32 deviceId);

	static float JslGetPollRate(int32 deviceId);

	static float JslGetTimeSinceLastUpdate(int32 deviceId);

	// --- Gyro calibration -------------------------------------------------------------------------------
	//
	// A gyroscope reports a small non-zero rotation even when perfectly still, so a controller left alone
	// will slowly drift. Calibrating measures that offset while the controller is still and subtracts it.
	//
	// Most games only need JSL4USetGyroCalibrationMode(Automatic) once per controller, and nothing else here:
	// the controller then works out on its own when it is being held still and keeps itself calibrated. The
	// Start/Stop/Reset nodes exist for driving an explicit "hold still while we calibrate" step in an options
	// screen, and only do anything in Manual mode.

	/**
	 * Chooses whether this controller calibrates its gyro on its own or only when told to.
	 * @param ConnectionId  The controller (see JSL4UGetConnectedControllers).
	 * @param Mode      Automatic for the set-and-forget behaviour most games want; Manual to drive it yourself.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Gyro Calibration",
		meta = (DisplayName = "JSL4U Set Gyro Calibration Mode", ToolTip = "TYPICAL USE: none -- every controller already starts in Automatic, which keeps the drift offset current by noticing when the controller is being held still. Call this only to opt out, with Manual, which hands calibration entirely to the Start/Stop nodes for a game that wants an explicit 'hold still' step."))
	static void JSL4USetGyroCalibrationMode(int64 ConnectionId, EJSL4UGyroCalibrationMode Mode);

	// Begins gathering samples for the gyro's drift offset. Call with the controller sitting still, and call
	// JSL4UStopManualGyroCalibration when you're done -- the longer it gathers, the better the offset. Only
	// meaningful in Manual mode.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Gyro Calibration",
		meta = (DisplayName = "JSL4U Start Manual Gyro Calibration", ToolTip = "Starts collecting gyro drift samples; the controller must stay still until Stop. Only meaningful in Manual mode -- in Automatic the controller already does this for itself, so most games never call this."))
	static void JSL4UStartManualGyroCalibration(int64 ConnectionId);

	// Stops gathering samples. The offset measured so far stays in effect.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Gyro Calibration",
		meta = (DisplayName = "JSL4U Stop Manual Gyro Calibration", ToolTip = "Stops collecting manual calibration samples and keeps the measured offset. Pairs with Start Manual Gyro Calibration; like it, unnecessary in Automatic mode."))
	static void JSL4UStopManualGyroCalibration(int64 ConnectionId);

	// Throws away the offset gathered so far and starts over. Use this when a calibration was taken while the
	// controller was in fact being moved, which leaves the gyro worse off than no calibration at all.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Gyro Calibration",
		meta = (DisplayName = "JSL4U Reset Gyro Calibration", ToolTip = "Discards the current drift offset and starts over. This is the one manual node worth exposing to players even in Automatic mode: an offset measured while the controller was moving leaves the gyro worse than no calibration, and this is the way out. Good behind a 'Recalibrate gyro' button."))
	static void JSL4UResetGyroCalibration(int64 ConnectionId);

	// This controller's calibration state, for driving a calibration screen (progress, "hold still" prompts).
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Gyro Calibration",
		meta = (DisplayName = "JSL4U Get Gyro Calibration Status", ToolTip = "Returns calibration mode, confidence, whether the controller reads as steady right now, and whether a manual calibration is running. Needed only to drive a calibration screen's prompts and progress; games without one never call it."))
	static FJSL4UGyroCalibrationStatus JSL4UGetGyroCalibrationStatus(int64 ConnectionId);

	/**
	 * The gyro drift offset currently being subtracted, in the same axes as JSL4UGetIMUState's Gyro.
	 * Save this per controller to restore a calibration between sessions, so a returning player doesn't have
	 * to calibrate again. Pairs exactly with JSL4USetGyroCalibrationOffset.
	 */
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Gyro Calibration",
		meta = (DisplayName = "JSL4U Get Gyro Calibration Offset", ToolTip = "Returns the drift offset, in the same Unreal axes as Get IMU State. Only useful if the game saves settings per controller, to spare a returning player a fresh calibration -- otherwise ignore this pair."))
	static FVector JSL4UGetGyroCalibrationOffset(int64 ConnectionId);

	// Restores an offset previously read with JSL4UGetGyroCalibrationOffset.
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Gyro Calibration",
		meta = (DisplayName = "JSL4U Set Gyro Calibration Offset", ToolTip = "Restores a gyro drift offset previously returned by Get Gyro Calibration Offset."))
	static void JSL4USetGyroCalibrationOffset(int64 ConnectionId, FVector Offset);

	// calibration
	static void JslResetContinuousCalibration(int32 deviceId);

	static void JslStartContinuousCalibration(int32 deviceId);

	static void JslPauseContinuousCalibration(int32 deviceId);

	static void JslSetAutomaticCalibration(int32 deviceId, bool enabled);

	static void JslGetCalibrationOffset(int32 deviceId, float& xOffset, float& yOffset, float& zOffset);

	static void JslSetCalibrationOffset(int32 deviceId, float xOffset, float yOffset, float zOffset);

	static FJSLAutoCalibration JslGetAutoCalibrationStatus(int32 deviceId);

	// Everything the plugin knows about one controller. Returns a struct with bIsConnected == false if
	// no controller has this connection id.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Controllers",
		meta = (DisplayName = "JSL4U Get Controller Info", ToolTip = "Returns this controller's connection id, Unreal input identities, model, assignment and current settings. Is Connected is false for a Connection Id that names no live controller."))
	static FJSL4UControllerInfo JSL4UGetControllerInfo(int64 ConnectionId);

	// super-getter for reading a whole lot of state at once
	static FJSLSettings JslGetControllerInfoAndSettings(int32 deviceId);

	// what kind of controller is this?
	static int32 JslGetControllerType(int32 deviceId);

	// is this a left, right, or full controller?
	static int32 JslGetControllerSplitType(int32 deviceId);

	// what colour is the controller (not all controllers support this; those that don't will report white)
	static FColor JslGetControllerColor(int32 InDeviceId);

	/**
	 * Sets the controller's light: the DualShock 4's light bar or the DualSense's. Controllers without a
	 * settable light ignore this (Switch controllers report a fixed body colour instead -- see
	 * JSL4UGetConnectedControllers).
	 * @param ConnectionId  The controller (see JSL4UGetConnectedControllers).
	 * @param Color     The colour to display. Alpha is ignored.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Output",
		meta = (DisplayName = "JSL4U Set Light Color", ToolTip = "Sets a DualShock 4 or DualSense light color. Controllers without a controllable light ignore this call."))
	static void JSL4USetLightColor(int64 ConnectionId, FColor Color);

	/**
	 * Sets the controller's rumble motors directly, 0 (off) to 1 (maximum intensity).
	 *
	 * You often don't need this node. These controllers work with Unreal's own force feedback, so
	 * "Play Force Feedback Effect" and "Client Play Force Feedback" drive them exactly as they drive an
	 * Xbox pad -- which gets you authored effects with curves, falloff and looping for free, and one code
	 * path for every gamepad. Both reach the same maximum: force feedback is clamped to 0-1 and 1 arrives
	 * here as full strength, so an effect that feels weak is a weak curve in the asset, not a limit of the
	 * plugin.
	 *
	 * Three things this node does that force feedback cannot, because force feedback is aimed at a *player*:
	 *  - Rumble one specific controller. Both halves of a joined Joy-Con pair are one player but two device
	 *    ids, so only this can buzz just the left one.
	 *  - Rumble a controller that is not assigned to a player at all. A controller-assignment screen
	 *    usually wants "press here and feel which controller this is" before any players exist, and force
	 *    feedback delivers nothing to a slot with no local player behind it.
	 *  - Hold a constant intensity without authoring a looping effect asset.
	 *
	 * BigRumble drives the heavy/low-frequency motor (strong shake, e.g. explosions, impacts);
	 * SmallRumble drives the light/high-frequency motor (fine buzz, e.g. UI feedback, engines).
	 * The rumble stays at the given intensities until you call this again -- call it with (0, 0) to stop.
	 * This and Unreal's force feedback write the same two values, so whichever ran most recently wins.
	 *
	 * Supported controllers: DualShock 4, DualSense, Joy-Cons and Pro Controller (HD rumble, both values
	 * mapped to the low/high-frequency actuator components).
	 * The Switch 2 Pro Controller does NOT support amplitude: it plays a fixed vibration preset while either
	 * value is above 0 and stops at (0, 0), so it is effectively on/off. Its amplitude-accurate rumble
	 * channel hasn't been mapped over USB yet, which means an effect that fades in or out feels like a flat
	 * buzz on that controller.
	 *
	 * The packet goes out from the controller's own polling thread, so this never blocks the game thread;
	 * it takes effect on that controller's next report (well under a frame for a connected controller).
	 * @param ConnectionId  The controller (see JSL4UGetConnectedControllers).
	 * @param SmallRumble  High-frequency motor intensity, 0-1.
	 * @param BigRumble    Low-frequency motor intensity, 0-1.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Output",
		meta = (DisplayName = "JSL4U Set Controller Rumble", ToolTip = "Sets one controller's high-frequency and low-frequency rumble, from 0 to 1; call with both at zero to stop. Drives that device directly, so it works before the controller belongs to any player -- buzzing a pad on an assignment screen so the player can tell which one they are holding. For authored gameplay effects, use Unreal Force Feedback instead: it is per-player, and it drives this plugin's controllers and XInput pads from the same effect."))
	static void JSL4USetControllerRumble(int64 ConnectionId, float SmallRumble, float BigRumble);

	// The channel Unreal's own force feedback writes to, kept separate from JSL4USetControllerRumble's so the two
	// cannot cancel each other -- the engine pushes force feedback values every frame, zeroes included.
	// Not exposed to Blueprint: games drive this through Play Force Feedback Effect and friends.
	static void SetForceFeedbackRumble(int32 DeviceHandle, int32 SmallRumble, int32 BigRumble);

	// --- Addressed by library handle, for the plugin's own insides only --------------------------------
	//
	// The handle is the JoyShockLibrary's key and it is reused: the controller it names today is not
	// necessarily the one it named a moment ago. That is exactly why nothing outside the plugin is given
	// one -- see FJSL4UControllerInfo::ConnectionId. These exist for the paths that START from the library
	// side and already hold a handle from the callback that raised them (the connect and disconnect
	// callbacks, the polling threads, the player-slot refresh), where translating to a connection id and
	// straight back would only add a lookup and a way to be wrong.
	static FJSL4UControllerInfo GetControllerInfoForHandle(int32 DeviceHandle);
	static FJSL4UMotionState GetMotionStateForHandle(int32 DeviceHandle);
	static void SetPlayerIndicatorForHandle(int32 DeviceHandle, int32 Number);

	// The rotation that takes a reading out of a sideways Joy-Con's frame and into the upright one it would
	// have had in a pair. Identity for everything else. Pure maths, no locks: the library's getters and the
	// interface's Enhanced Input dispatch both apply it, and calling one function is what stops the two from
	// disagreeing about which way a horizontal Joy-Con is pointing.
	static FQuat GetJoyConGripUndoRotation(bool bHorizontal, bool bIsLeft);

	/**
	 * Sets the controller's player number indicator (the DualSense's player LEDs, or a Switch controller's
	 * row of LEDs). The DualShock 4 has no such indicator and ignores this.
	 * @param ConnectionId  The controller (see JSL4UGetConnectedControllers).
	 * @param Number    The player number to show, counting from 1.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Output",
		meta = (DisplayName = "JSL4U Set Player Indicator", ToolTip = "Shows a one-based player number on Switch, Switch 2 or DualSense player LEDs. The DualShock 4 has no numeric indicator and ignores this call; use Set Light Color for its RGB light bar."))
	static void JSL4USetPlayerIndicator(int64 ConnectionId, int32 Number);

	/**
	 * Sets the blue HOME ring light on a right Joy-Con or Pro Controller (0 = off, 1 = full brightness).
	 * Other controllers ignore this.
	 *
	 * This is Nintendo's *notification* light, not a player indicator -- on a real Switch it stays off
	 * during play and the system uses it for pairing, low battery and lost-connection feedback. The four
	 * green player LEDs are the player identity (see Set Player Indicator); driving this one instead is
	 * likely to read as a system message rather than as "you are player 2".
	 *
	 * The plugin switches this light off once, when the controller comes online. Calling this hands
	 * ownership to the game permanently: that automatic clear stops for the controller, and the light then
	 * holds whatever the game last set -- including zero. There is no way back to automatic for the rest of
	 * the connection, which is deliberate: a game that has opinions about this light should not have them
	 * silently overwritten.
	 *
	 * Every call is sent to the controller, even one that asks for the brightness the light is already
	 * believed to have. The firmware turns this light back on by itself (a reconnect or a battery
	 * notification will do it), so re-asserting a value the plugin thinks is already set is a real request
	 * and not a no-op -- which is what makes "set it to 0" reliable for turning the light off.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Output",
		meta = (DisplayName = "JSL4U Set Home Light", ToolTip = "Sets the HOME ring light brightness (0-1) on a right Joy-Con or Pro Controller. This is a notification light, not a player indicator. Calling it stops the plugin's automatic keep-it-off upkeep for that controller."))
	static void JSL4USetHomeLight(int64 ConnectionId, float Brightness);

	/**
	 * The extra sensors a Switch 2 controller carries: battery voltage, temperature, the magnetometer and
	 * the optical mouse sensor in its underside.
	 *
	 * Motion is NOT here -- read the accelerometer and gyroscope through the usual IMU nodes, which every
	 * controller this plugin drives answers. This is only for readings no other family reports.
	 *
	 * Any other controller returns Is Supported false with every field zero, so this is safe to call
	 * without checking the controller's model first.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Controllers",
		meta = (DisplayName = "JSL4U Get Switch 2 Sensors", ToolTip = "Battery voltage, temperature, magnetometer and mouse sensor from a Switch 2 controller. Motion is not here -- use the IMU nodes for that. Other controllers return Is Supported false."))
	static FJSL4USwitch2Sensors JSL4UGetSwitch2Sensors(int64 ConnectionId);

	// Converts the same semantic one-based number into Nintendo's four visible LED states. This is useful
	// for controller mirrors and UI; the physical controller is updated automatically by assignment.
	UFUNCTION(BlueprintPure, Category = "JoyShock Library|Output",
		meta = (DisplayName = "JSL4U Get Switch Player LED Pattern", ToolTip = "Returns the four Nintendo player LEDs for player numbers 1-8, matching Joy-Cons, Switch Pro and Switch 2 Pro Controllers."))
	static void JSL4UGetSwitchPlayerLedPattern(int32 PlayerNumber,
		bool& bLed1, bool& bLed2, bool& bLed3, bool& bLed4);

	// --- Low-level C++ compatibility helpers. Not exposed to Blueprint. ---

	// set controller light colour (not all controllers have a light whose colour can be set, but that just means nothing will be done when this is called -- no harm)
	static void JslSetLightColor(int32 InDeviceId, FColor InColor);

	// set controller rumble, 0-255 per motor. See JSL4USetControllerRumble for the full description.
	static void JslSetRumble(int32 deviceId, int32 smallRumble, int32 bigRumble);

	// set controller player number indicator (not all controllers have a number indicator which can be set, but that just means nothing will be done when this is called -- no harm)
	static void JslSetPlayerNumber(int32 deviceId, int32 number);

};
