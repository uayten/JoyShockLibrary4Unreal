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
	// bar), as set by JslSetPlayerNumber. This is a display value, not an identity -- to identify a
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
	static int32 JSL4UGetPlayerIndex(int32 DeviceId);

	// The inverse of JSL4UGetPlayerIndex: every controller currently feeding a player slot. Two entries for
	// a joined Joy-Con pair (rumble both to rumble "the player"), one for a standalone controller, none if
	// nothing is assigned to that slot. PlayerIndex is a platform user index -- if you have a
	// PlayerController, prefer JSL4UGetControllersForPlayerController, which converts it for you.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JoyShockLibrary|JoyConPairing")
	static TArray<FJSL4UControllerInfo> JSL4UGetControllersForPlayer(int32 PlayerIndex);

	// The controller(s) of the player behind a PlayerController -- i.e. of whoever issued the command you
	// are reacting to. Defaults to self inside a PlayerController Blueprint, so this is the one-node answer
	// to "which controller is this player holding?" (e.g. to rumble it).
	// Note: do NOT build this out of "Get Player Controller ID". That is the legacy controller id, which is
	// a different number from the platform user index that player slots are assigned from -- this converts
	// through the same IPlatformInputDeviceMapper the assignment uses.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JoyShockLibrary|JoyConPairing", meta = (DefaultToSelf = "PlayerController"))
	static TArray<FJSL4UControllerInfo> JSL4UGetControllersForPlayerController(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static int32 JslConnectDevices();

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static int32 JslGetConnectedDeviceHandles(/* int* */ TArray<int32>& OutDeviceHandleArray); //, int32 InSize);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslDisconnectAndDisposeAll();

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
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
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJoyShockState JslGetSimpleState(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UJoyShockState JSL4UGetSimpleState(int32 DeviceId);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FIMUState JslGetIMUState(int32 deviceId);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UIMUState JSL4UGetIMUState(int32 DeviceID);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UIMUState JSL4UGetRawIMUState(int32 DeviceID);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FMotionState JslGetMotionState(int32 deviceId);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UMotionState JSL4UGetMotionState(int32 DeviceID);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UMotionState JSL4UGetRawMotionState(int32 DeviceID);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FTouchState JslGetTouchState(int32 deviceId, bool previous = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UTouchState JSL4UGetTouchState(int32 DeviceId, bool bPrevious = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static bool JslGetTouchpadDimension(int32 deviceId, int32 &sizeX, int32 &sizeY);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static int32 JslGetButtons(int32 deviceId);

	// get thumbsticks

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FVector2D JSL4UGetLeftStick(int32 DeviceId);
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetLeftX(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetLeftY(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FVector2D JSL4UGetRightStick(int32 DeviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetRightX(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetRightY(int32 deviceId);

	// get triggers. Switch controllers don't have analogue triggers, but will report 0.0 or 1.0 so they can be used in the same way as others
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetLeftTrigger(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetRightTrigger(int32 deviceId);

	// get gyro
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetGyroX(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetGyroY(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetGyroZ(int32 deviceId);

	// get accumulated average gyro since this function was last called or last flushed values
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslGetAndFlushAccumulatedGyro(int32 deviceId, float& gyroX, float& gyroY, float& gyroZ);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static FVector JSL4UGetAndFlushAccumulatedGyro(int32 InDeviceId);

	// set gyro space. JslGetGyro*, JslGetAndFlushAccumulatedGyro, JslGetIMUState, and the IMU_STATEs reported in the callback functions will use one of 3 transformations:
	// 0 = local space -> no transformation is done on gyro input
	// 1 = world space -> gyro input is transformed based on the calculated gravity direction to account for the player's preferred controller orientation
	// 2 = player space -> a simple combination of local and world space that is as adaptive as world space but is as robust as local space
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslSetGyroSpace(int32 deviceId, int32 gyroSpace);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JSL4USetGyroSpace(int32 InDeviceID, EJSL4UGyroSpace InGyroSpace);

	// get accelerometer
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetAccelX(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetAccelY(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetAccelZ(int32 deviceId);

	// get touchpad
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static int32 JslGetTouchId(int32 deviceId, bool secondTouch = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static bool JslGetTouchDown(int32 deviceId, bool secondTouch = false);

	// NEW FUNCTION
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FVector2D JSL4UGetTouch(int32 DeviceId, bool bSecondTouch = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetTouchX(int32 deviceId, bool secondTouch = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetTouchY(int32 deviceId, bool secondTouch = false);

	// analog parameters have different resolutions depending on device
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetStickStep(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetTriggerStep(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetPollRate(int32 deviceId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static float JslGetTimeSinceLastUpdate(int32 deviceId);

	// calibration
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslResetContinuousCalibration(int32 deviceId);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslStartContinuousCalibration(int32 deviceId);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslPauseContinuousCalibration(int32 deviceId);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslSetAutomaticCalibration(int32 deviceId, bool enabled);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static void JslGetCalibrationOffset(int32 deviceId, float& xOffset, float& yOffset, float& zOffset);

	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslSetCalibrationOffset(int32 deviceId, float xOffset, float yOffset, float zOffset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSLAutoCalibration JslGetAutoCalibrationStatus(int32 deviceId);

	// Everything the plugin knows about one controller. Returns a struct with bIsConnected == false if
	// no controller has this device id.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSL4UControllerInfo JSL4UGetControllerInfoAndSettings(int32 DeviceId);

	// super-getter for reading a whole lot of state at once
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FJSLSettings JslGetControllerInfoAndSettings(int32 deviceId);

	// what kind of controller is this?
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static int32 JslGetControllerType(int32 deviceId);

	// is this a left, right, or full controller?
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static int32 JslGetControllerSplitType(int32 deviceId);

	// what colour is the controller (not all controllers support this; those that don't will report white)
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = JoyShockLibrary)
	static FColor JslGetControllerColor(int32 InDeviceId);

	// set controller light colour (not all controllers have a light whose colour can be set, but that just means nothing will be done when this is called -- no harm)
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslSetLightColor(int32 InDeviceId, FColor InColor);

	/**
	 * Sets the controller's rumble motors. Values range from 0 (off) to 255 (maximum intensity).
	 * BigRumble drives the heavy/low-frequency motor (strong shake, e.g. explosions, impacts);
	 * SmallRumble drives the light/high-frequency motor (fine buzz, e.g. UI feedback, engines).
	 * The rumble stays at the given intensities until you call this again -- call it with (0, 0) to stop.
	 * Supported controllers: DualShock 4, DualSense, Joy-Cons and Pro Controller (HD rumble, both values
	 * mapped to the low/high-frequency actuator components), and Switch 2 Pro Controller over USB (currently
	 * plays a fixed vibration preset while either value is above 0; amplitude control is not mapped yet).
	 * @param deviceId     The controller's device id (see JSL4UGetConnectedControllers).
	 * @param smallRumble  High-frequency motor intensity, 0-255.
	 * @param bigRumble    Low-frequency motor intensity, 0-255.
	 */
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslSetRumble(int32 deviceId, int32 smallRumble, int32 bigRumble);

	// set controller player number indicator (not all controllers have a number indicator which can be set, but that just means nothing will be done when this is called -- no harm)
	UFUNCTION(BlueprintCallable, Category = JoyShockLibrary)
	static void JslSetPlayerNumber(int32 deviceId, int32 number);
	
};
