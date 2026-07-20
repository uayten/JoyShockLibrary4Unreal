// JoyShockLibrary.h - Contains declarations of functions
#pragma once
#include <shared_mutex>
#include "Containers/StaticArray.h"
#include "JoyShockLibrary.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogJoyShockLibrary, Verbose, All);

class APlayerController;

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

UENUM(BlueprintType)
enum class EJSL4UControllerType : uint8
{
	Undefined = 0 UMETA(DisplayName = "Undefined"),
	JoyConLeft = 1 UMETA(DisplayName = "JoyCon (L)"),
	JoyConRight = 2 UMETA(DisplayName = "JoyCon (R)"),
	ProController = 3 UMETA(DisplayName = "Pro Controller"),
	DualShock4 = 4 UMETA(DisplayName = "DualShock 4"),
	DualSense = 5 UMETA(DisplayName = "DualSense"),
	ProController2 = 6 UMETA(DisplayName = "Pro Controller 2")
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
	// Calibration only happens between JSL4UStartGyroCalibration and JSL4UStopGyroCalibration. Use this to
	// drive an explicit "put the controller down and hold still" step in an options screen.
	Manual UMETA(DisplayName = "Manual"),
};

/** A controller's current gyro calibration state, for showing progress in a calibration screen. */
USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4UGyroCalibrationStatus
{
	GENERATED_BODY()

	// How sure the automatic calibration is of its current offset, 0 to 1. Meaningless in Manual mode.
	UPROPERTY(BlueprintReadOnly)
	float Confidence = 0.f;

	// Whether the controller currently reads as being held still. In a manual calibration screen this is
	// the cue for "hold still" versus "keep holding".
	UPROPERTY(BlueprintReadOnly)
	bool bIsSteady = false;

	// The mode this controller is in (see JSL4USetGyroCalibrationMode).
	UPROPERTY(BlueprintReadOnly)
	EJSL4UGyroCalibrationMode Mode = EJSL4UGyroCalibrationMode::Manual;

	// Whether a manual calibration is currently gathering samples, i.e. JSL4UStartGyroCalibration was
	// called and JSL4UStopGyroCalibration has not been.
	UPROPERTY(BlueprintReadOnly)
	bool bIsCalibrating = false;
};

// Everything the plugin knows about a controller: its identity, its type, the player slot it feeds and
// its live JSL settings. Returned for a single device by JSL4UGetControllerInfoAndSettings and for every
// connected device by JSL4UGetConnectedControllers.
USTRUCT(BlueprintType)
struct JOYSHOCKLIBRARY4UNREAL_API FJSL4UControllerInfo // typedef struct JSL_SETTINGS {
{
	GENERATED_BODY()

	// Device id / handle (0, 1, 2, ...). This is what every other Jsl* / JSL4U* function takes.
	UPROPERTY(BlueprintReadOnly)
	int32 DeviceId = 0;

	UPROPERTY(BlueprintReadOnly)
	EJSL4UControllerType ControllerType = EJSL4UControllerType::Undefined;

	// The player slot (0, 1, 2, ...) this controller's input is delivered to, or -1 if it isn't connected.
	// Both halves of a joined Joy-Con pair share the same PlayerIndex.
	UPROPERTY(BlueprintReadOnly)
	int32 PlayerIndex = -1;

	// If this controller is joined with another, the device id of its partner; otherwise -1.
	UPROPERTY(BlueprintReadOnly)
	int32 JoinedToDeviceId = -1;

	// The number shown on the controller's player indicator (the Switch player LEDs, the DualSense light
	// bar), as set by JSL4USetPlayerNumber. This is a display value, not an identity -- to identify a
	// controller use DeviceId, and to know which player it feeds use PlayerIndex.
	UPROPERTY(BlueprintReadOnly)
	int32 PlayerLedNumber = 0;

	UPROPERTY(BlueprintReadOnly)
	FColor Color = FColor::Black;

	UPROPERTY(BlueprintReadOnly)
	int32 GyroSpace = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 SplitType = 0;

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

UCLASS()
class JOYSHOCKLIBRARY4UNREAL_API UJoyShockLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Returns every currently connected controller, sorted by device id. Use this to list controllers
	// (e.g. "0 JoyCon (R), 1 DualSense, 2 JoyCon (L)") and pick which Joy-Cons to join. Use
	// "Enum to String" on ControllerType for a readable name.
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|JoyConPairing")
	static TArray<FJSL4UControllerInfo> JSL4UGetConnectedControllers();

	// True for controller types that can be joined into a pair -- currently the left and right Joy-Cons.
	// This is the single source of truth for "can this be joined": JSL4UJoinJoyCons validates with it too.
	UFUNCTION(BlueprintPure, Category = "JoyShockLibrary|JoyConPairing")
	static bool JSL4UIsJoinable(EJSL4UControllerType ControllerType);

	// Joins two Joy-Cons so they act as a single controller for one player: their inputs are merged and
	// delivered to the lower device id's player (e.g. joining 0 and 2 -> both feed player 0). Both ids must
	// be Joy-Cons (one left, one right). Returns true on success.
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|JoyConPairing")
	static bool JSL4UJoinJoyCons(int32 DeviceIdA, int32 DeviceIdB);

	// Undoes a join involving this device id (both halves go back to being their own players).
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|JoyConPairing")
	static void JSL4UUnjoinJoyCon(int32 DeviceId);

	// Undoes every Joy-Con join.
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|JoyConPairing")
	static void JSL4UUnjoinAllJoyCons();

	// Returns the player slot (0, 1, 2, ...) the given controller's input is delivered to, or -1 if it
	// isn't connected. Both halves of a joined Joy-Con pair return the same slot. Useful for UI.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JoyShockLibrary|JoyConPairing")
	static int32 JSL4UGetPlayerIndexOfController(int32 DeviceId);

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
	 * @param DeviceId     The controller to assign (see JSL4UGetConnectedControllers).
	 * @param PlayerIndex  The player slot to put it on, counting from 0. Pass -1 to hand the controller
	 *                     back to automatic assignment.
	 * @return False if DeviceId is not a connected controller.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|JoyConPairing")
	static bool JSL4UAssignControllerToPlayerIndex(int32 DeviceId, int32 PlayerIndex);

	// Assigns a controller to the player behind a PlayerController -- the setter counterpart of
	// JSL4UGetControllersOfPlayer, and the one-node answer to "make this controller drive this
	// player". Same caveat as the getter: do NOT build this out of "Get Player Controller ID", which is the
	// legacy controller id rather than the platform user index slots are assigned from.
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|JoyConPairing", meta = (DefaultToSelf = "PlayerController"))
	static bool JSL4UAssignControllerToPlayer(int32 DeviceId, APlayerController* PlayerController);

	// The inverse of JSL4UGetPlayerIndexOfController: every controller currently feeding a player slot. Two entries for
	// a joined Joy-Con pair (rumble both to rumble "the player"), one for a standalone controller, none if
	// nothing is assigned to that slot. PlayerIndex is a platform user index -- if you have a
	// PlayerController, prefer JSL4UGetControllersOfPlayer, which converts it for you.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JoyShockLibrary|JoyConPairing")
	static TArray<FJSL4UControllerInfo> JSL4UGetControllersOfPlayerIndex(int32 PlayerIndex);

	// The controller(s) of the player behind a PlayerController -- i.e. of whoever issued the command you
	// are reacting to. Defaults to self inside a PlayerController Blueprint, so this is the one-node answer
	// to "which controller is this player holding?" (e.g. to rumble it).
	// Note: do NOT build this out of "Get Player Controller ID". That is the legacy controller id, which is
	// a different number from the platform user index that player slots are assigned from -- this converts
	// through the same IPlatformInputDeviceMapper the assignment uses.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JoyShockLibrary|JoyConPairing", meta = (DefaultToSelf = "PlayerController"))
	static TArray<FJSL4UControllerInfo> JSL4UGetControllersOfPlayer(APlayerController* PlayerController);

	/**
	 * Asks the plugin to re-scan for controllers, on a background thread.
	 *
	 * You rarely need this: controllers are picked up automatically at startup and whenever Windows reports a
	 * device change. It exists for the cases that produce no device-change message -- and it returns
	 * immediately, with the scan happening off the game thread, so it is safe to call from gameplay.
	 * Repeated calls while a scan is running are coalesced into one follow-up pass.
	 */
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JSL4URefreshControllers();

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4URefreshControllers instead. This node enumerates HID devices synchronously on the calling thread, which freezes the game while it runs."))
	static int32 JslConnectDevices();

	/**
	 * Fills OutDeviceHandleArray with the device id of every connected controller and returns how many
	 * there were. The device id is the handle every other Jsl* / JSL4U* node takes.
	 * "Connected" means the controller has actually delivered input, not merely that it turned up in HID
	 * enumeration -- a controller that has just been switched off can linger in enumeration for a moment,
	 * and is deliberately not listed (nor reported as connected) during that window.
	 * For new Blueprints, prefer JSL4UGetConnectedControllers: it returns the same handles (as DeviceId)
	 * plus each controller's type, player slot and settings, so you rarely need this raw handle list.
	 */
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetConnectedControllers instead, which returns the same handles as DeviceId plus each controller's type, player slot and settings."))
	static int32 JslGetConnectedDeviceHandles(/* int* */ TArray<int32>& OutDeviceHandleArray); //, int32 InSize);

	/**
	 * Whether this device id currently refers to a connected, working controller.
	 *
	 * Agrees with JSL4UGetConnectedControllers: a device that turned up in enumeration but has not delivered
	 * input (a controller that has just been switched off can linger there for a moment) reports false here
	 * too, where the legacy JslStillConnected reports true.
	 *
	 * Note that a device id is not a lasting identity for a physical controller -- ids are reused once
	 * freed, so a true here can mean a *different* controller has taken that id. Prefer reacting to the
	 * JoyShock subsystem's connect/disconnect events over polling this; it is meant for cheaply validating
	 * an id you are already holding on to, without building the whole controller list to look it up.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static bool JSL4UIsControllerConnected(int32 DeviceId);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UIsControllerConnected instead."))
	static bool JslStillConnected(int32 deviceId);

	// TODO: Remove temporary debug function
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|Debug", meta = (WorldContext = "WorldContextObject"))
	static int32 GetNumPlayerControllers(const UObject* WorldContextObject)
	{
		return WorldContextObject->GetWorld()->GetNumPlayerControllers();
	}

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
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetSimpleState instead."))
	static FJoyShockState JslGetSimpleState(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UJoyShockState JSL4UGetSimpleState(int32 DeviceId);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetIMUState instead (or JSL4UGetRawIMUState for the untransformed values)."))
	static FIMUState JslGetIMUState(int32 deviceId);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UIMUState JSL4UGetIMUState(int32 DeviceID);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UIMUState JSL4UGetRawIMUState(int32 DeviceID);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetMotionState instead (or JSL4UGetRawMotionState for the untransformed values)."))
	static FMotionState JslGetMotionState(int32 deviceId);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UMotionState JSL4UGetMotionState(int32 DeviceID);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UMotionState JSL4UGetRawMotionState(int32 DeviceID);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetTouchState instead."))
	static FTouchState JslGetTouchState(int32 deviceId, bool previous = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UTouchState JSL4UGetTouchState(int32 DeviceId, bool bPrevious = false);

	// The touchpad's size in its own units (1920 x 943 on the DualShock 4 and DualSense), or zero for a
	// controller without one. JSL4UGetTouchState reports touches normalised to 0-1, so multiply by this if
	// you need touchpad-native coordinates -- e.g. to keep a drag's aspect ratio right.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FVector2D JSL4UGetTouchpadSize(int32 DeviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetTouchpadSize instead, which returns a Vector2D."))
	static bool JslGetTouchpadDimension(int32 deviceId, int32 &sizeX, int32 &sizeY);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetSimpleState instead and read its Buttons field."))
	static int32 JslGetButtons(int32 deviceId);

	// get thumbsticks

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FVector2D JSL4UGetLeftStick(int32 DeviceId);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetLeftStick instead, which returns both axes as a Vector2D."))
	static float JslGetLeftX(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetLeftStick instead, which returns both axes as a Vector2D."))
	static float JslGetLeftY(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FVector2D JSL4UGetRightStick(int32 DeviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetRightStick instead, which returns both axes as a Vector2D."))
	static float JslGetRightX(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetRightStick instead, which returns both axes as a Vector2D."))
	static float JslGetRightY(int32 deviceId);

	// get triggers. Switch controllers don't have analogue triggers, but will report 0.0 or 1.0 so they can be used in the same way as others
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetSimpleState instead and read its LeftTrigger field."))
	static float JslGetLeftTrigger(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetSimpleState instead and read its RightTrigger field."))
	static float JslGetRightTrigger(int32 deviceId);

	// get gyro
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetIMUState instead and read its Gyro field."))
	static float JslGetGyroX(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetIMUState instead and read its Gyro field."))
	static float JslGetGyroY(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetIMUState instead and read its Gyro field."))
	static float JslGetGyroZ(int32 deviceId);

	// get accumulated average gyro since this function was last called or last flushed values
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetAndFlushAccumulatedGyro instead, which returns a Vector in Unreal's axes."))
	static void JslGetAndFlushAccumulatedGyro(int32 deviceId, float& gyroX, float& gyroY, float& gyroZ);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static FVector JSL4UGetAndFlushAccumulatedGyro(int32 InDeviceId);

	// set gyro space. JslGetGyro*, JslGetAndFlushAccumulatedGyro, JslGetIMUState, and the IMU_STATEs reported in the callback functions will use one of 3 transformations:
	// 0 = local space -> no transformation is done on gyro input
	// 1 = world space -> gyro input is transformed based on the calculated gravity direction to account for the player's preferred controller orientation
	// 2 = player space -> a simple combination of local and world space that is as adaptive as world space but is as robust as local space
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4USetGyroSpace instead, which takes an EJSL4UGyroSpace rather than a raw integer."))
	static void JslSetGyroSpace(int32 deviceId, int32 gyroSpace);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JSL4USetGyroSpace(int32 InDeviceID, EJSL4UGyroSpace InGyroSpace);

	// get accelerometer
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetIMUState instead and read its Acceleration field."))
	static float JslGetAccelX(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetIMUState instead and read its Acceleration field."))
	static float JslGetAccelY(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetIMUState instead and read its Acceleration field."))
	static float JslGetAccelZ(int32 deviceId);

	// get touchpad
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetTouchState instead and read the TouchID of the touch you want."))
	static int32 JslGetTouchId(int32 deviceId, bool secondTouch = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetTouchState instead and read the bIsDown of the touch you want."))
	static bool JslGetTouchDown(int32 deviceId, bool secondTouch = false);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FVector2D JSL4UGetTouch(int32 DeviceId, bool bSecondTouch = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetTouch instead, which returns both axes as a Vector2D."))
	static float JslGetTouchX(int32 deviceId, bool secondTouch = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetTouch instead, which returns both axes as a Vector2D."))
	static float JslGetTouchY(int32 deviceId, bool secondTouch = false);

	// analog parameters have different resolutions depending on device
	// The smallest change this controller can report on a stick axis. Sticks are 8-bit on a DualShock 4 and
	// 12-bit on Switch controllers, so this differs per device -- useful for sizing a deadzone or an
	// on-screen readout to what the hardware can actually resolve.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JSL4UGetStickStep(int32 DeviceId);

	// The smallest change this controller can report on a trigger. Switch controllers have no analog
	// triggers and report 1 (fully on or fully off).
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JSL4UGetTriggerStep(int32 DeviceId);

	// How often this controller sends input reports, in milliseconds per report.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JSL4UGetPollRate(int32 DeviceId);

	// Seconds since this controller last sent an input report. A value climbing well past the poll rate means
	// the controller has gone quiet, which is the difference between the engine not routing its input and the
	// controller not sending any.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JSL4UGetTimeSinceLastUpdate(int32 DeviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetStickStep instead."))
	static float JslGetStickStep(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetTriggerStep instead."))
	static float JslGetTriggerStep(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetPollRate instead."))
	static float JslGetPollRate(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetTimeSinceLastUpdate instead."))
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
	 * @param DeviceId  The controller (see JSL4UGetConnectedControllers).
	 * @param Mode      Automatic for the set-and-forget behaviour most games want; Manual to drive it yourself.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|Gyro")
	static void JSL4USetGyroCalibrationMode(int32 DeviceId, EJSL4UGyroCalibrationMode Mode);

	// Begins gathering samples for the gyro's drift offset. Call with the controller sitting still, and call
	// JSL4UStopGyroCalibration when you're done -- the longer it gathers, the better the offset. Only
	// meaningful in Manual mode.
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|Gyro")
	static void JSL4UStartGyroCalibration(int32 DeviceId);

	// Stops gathering samples. The offset measured so far stays in effect.
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|Gyro")
	static void JSL4UStopGyroCalibration(int32 DeviceId);

	// Throws away the offset gathered so far and starts over. Use this when a calibration was taken while the
	// controller was in fact being moved, which leaves the gyro worse off than no calibration at all.
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|Gyro")
	static void JSL4UResetGyroCalibration(int32 DeviceId);

	// This controller's calibration state, for driving a calibration screen (progress, "hold still" prompts).
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JoyShockLibrary|Gyro")
	static FJSL4UGyroCalibrationStatus JSL4UGetGyroCalibrationStatus(int32 DeviceId);

	/**
	 * The gyro drift offset currently being subtracted, in the same axes as JSL4UGetIMUState's Gyro.
	 * Save this per controller to restore a calibration between sessions, so a returning player doesn't have
	 * to calibrate again. Pairs exactly with JSL4USetGyroCalibrationOffset -- do not mix it with the legacy
	 * JslGetCalibrationOffset, which reports the same offset in the library's own axes rather than Unreal's.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JoyShockLibrary|Gyro")
	static FVector JSL4UGetGyroCalibrationOffset(int32 DeviceId);

	// Restores an offset previously read with JSL4UGetGyroCalibrationOffset.
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|Gyro")
	static void JSL4USetGyroCalibrationOffset(int32 DeviceId, FVector Offset);

	// calibration
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UResetGyroCalibration instead."))
	static void JslResetContinuousCalibration(int32 deviceId);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UStartGyroCalibration instead."))
	static void JslStartContinuousCalibration(int32 deviceId);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UStopGyroCalibration instead."))
	static void JslPauseContinuousCalibration(int32 deviceId);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4USetGyroCalibrationMode instead, which takes an EJSL4UGyroCalibrationMode."))
	static void JslSetAutomaticCalibration(int32 deviceId, bool enabled);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetGyroCalibrationOffset instead, which returns a Vector in Unreal's axes."))
	static void JslGetCalibrationOffset(int32 deviceId, float& xOffset, float& yOffset, float& zOffset);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4USetGyroCalibrationOffset instead, which takes a Vector in Unreal's axes."))
	static void JslSetCalibrationOffset(int32 deviceId, float xOffset, float yOffset, float zOffset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetGyroCalibrationStatus instead."))
	static FJSLAutoCalibration JslGetAutoCalibrationStatus(int32 deviceId);

	// Everything the plugin knows about one controller. Returns a struct with bIsConnected == false if
	// no controller has this device id.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UControllerInfo JSL4UGetControllerInfoAndSettings(int32 DeviceId);

	// super-getter for reading a whole lot of state at once
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetControllerInfoAndSettings instead."))
	static FJSLSettings JslGetControllerInfoAndSettings(int32 deviceId);

	// what kind of controller is this?
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetControllerInfoAndSettings instead and read its ControllerType field."))
	static int32 JslGetControllerType(int32 deviceId);

	// is this a left, right, or full controller?
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetControllerInfoAndSettings instead and read its SplitType field."))
	static int32 JslGetControllerSplitType(int32 deviceId);

	// what colour is the controller (not all controllers support this; those that don't will report white)
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4UGetControllerInfoAndSettings instead and read its Color field."))
	static FColor JslGetControllerColor(int32 InDeviceId);

	/**
	 * Sets the controller's light: the DualShock 4's light bar or the DualSense's. Controllers without a
	 * settable light ignore this (Switch controllers report a fixed body colour instead -- see
	 * JSL4UGetConnectedControllers).
	 * @param DeviceId  The controller's device id (see JSL4UGetConnectedControllers).
	 * @param Color     The colour to display. Alpha is ignored.
	 */
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JSL4USetLightColor(int32 DeviceId, FColor Color);

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
	 * @param DeviceId     The controller's device id (see JSL4UGetConnectedControllers).
	 * @param SmallRumble  High-frequency motor intensity, 0-1.
	 * @param BigRumble    Low-frequency motor intensity, 0-1.
	 */
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JSL4USetRumble(int32 DeviceId, float SmallRumble, float BigRumble);

	// The channel Unreal's own force feedback writes to, kept separate from JSL4USetRumble's so the two
	// cannot cancel each other -- the engine pushes force feedback values every frame, zeroes included.
	// Not exposed to Blueprint: games drive this through Play Force Feedback Effect and friends.
	static void SetForceFeedbackRumble(int32 DeviceId, int32 SmallRumble, int32 BigRumble);

	/**
	 * Sets the controller's player number indicator (the DualSense's player LEDs, or a Switch controller's
	 * row of LEDs). The DualShock 4 has no such indicator and ignores this.
	 * @param DeviceId  The controller's device id (see JSL4UGetConnectedControllers).
	 * @param Number    The player number to show, counting from 1.
	 */
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JSL4USetPlayerNumber(int32 DeviceId, int32 Number);

	// --- Legacy JSL setters. Prefer the JSL4U* equivalents above; these are thin wrappers kept for
	// compatibility and will be removed. ---

	// set controller light colour (not all controllers have a light whose colour can be set, but that just means nothing will be done when this is called -- no harm)
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4USetLightColor instead."))
	static void JslSetLightColor(int32 InDeviceId, FColor InColor);

	// set controller rumble, 0-255 per motor. See JSL4USetRumble for the full description.
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4USetRumble instead (0-1 per motor)."))
	static void JslSetRumble(int32 deviceId, int32 smallRumble, int32 bigRumble);

	// set controller player number indicator (not all controllers have a number indicator which can be set, but that just means nothing will be done when this is called -- no harm)
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary, meta = (DeprecatedFunction, DeprecationMessage = "Use JSL4USetPlayerNumber instead."))
	static void JslSetPlayerNumber(int32 deviceId, int32 number);

};
