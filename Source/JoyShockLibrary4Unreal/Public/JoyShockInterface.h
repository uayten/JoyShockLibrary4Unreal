#pragma once


#include "CoreTypes.h"
#include "Containers/Queue.h"
#include "GenericPlatform/IInputInterface.h"
#include "IInputDevice.h"
#include "JoyShockLibrary4UnrealSettings.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockLibrary.h"

// Max number of controller buttons.  Must be < 256
#define MAX_NUM_CONTROLLER_BUTTONS 27

struct FTouchState;
struct FJoyShockState;
struct FIMUState;
enum class FForceFeedbackChannelType;

// Interface class for JoyShock devices (DualShock 4, DualSense, Switch Pro Controller, JoyCons)
class FJoyShockInterface : public IInputDevice
{
public:
	static TSharedRef<FJoyShockInterface> Create(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
	
	/**
	 * Poll for controller state and send events if needed
	 */
	virtual void SendControllerEvents() override;

	virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;

	/**
	* Sets the strength/speed of the given channel for the given controller id.
	* NOTE: If the channel is not supported, the call will silently fail
	*
	* @param ControllerId the id of the controller whose value is to be set
	* @param ChannelType the type of channel whose value should be set
	* @param Value strength or speed of feedback, 0.0f to 1.0f. 0.0f will disable
	*/
	virtual void SetChannelValue(int32 ControllerId, const FForceFeedbackChannelType ChannelType, const float Value) override;

	/**
	* Sets the strength/speed of all the channels for the given controller id.
	* NOTE: Unsupported channels are silently ignored
	*
	* @param ControllerId the id of the controller whose value is to be set
	* @param Values strength or speed of feedback for all channels
	*/
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override;

	virtual bool IsGamepadAttached() const override { return bIsGamepadAttached; }
	virtual void Tick(float DeltaTime) override {};
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return false; }

	virtual ~FJoyShockInterface() override;

	// --- Joy-Con pairing (game thread only) --------------------------------------------------------------
	// Joins two connected device handles so they share one player slot (their input feeds a single player).
	// Returns false if the handles are equal or not both connected. Any existing joins involving either
	// handle are dissolved first.
	bool JoinControllers(int32 HandleA, int32 HandleB);
	// Dissolves any join involving this handle (both halves become their own players again).
	void UnjoinController(int32 Handle);
	// Dissolves every join.
	void UnjoinAllControllers();
	// Returns the handle joined with this one, or INDEX_NONE if it isn't joined.
	int32 GetJoinPartner(int32 Handle) const;
	// Returns the player slot (0, 1, 2, ...) this device's input is delivered to, or INDEX_NONE if
	// the device isn't connected.
	int32 GetPlayerIndexForDevice(int32 Handle) const;
	// Assigns this device's logical controller to a player slot, overriding the slot it was given on
	// connection. Pass INDEX_NONE to hand it back to automatic assignment. Returns false if the handle is
	// not a connected controller. Slots may be shared: assigning two controllers to one slot makes both
	// drive that player, which is what a joined Joy-Con pair already does.
	bool SetPlayerIndexForDevice(int32 Handle, int32 PlayerIndex);

private:
	FJoyShockInterface(const TSharedRef<FGenericApplicationMessageHandler>& MessageHandler);

	void InitializeAdditionalKeys();

	struct FControllerState
	{
		// If the controller is currently connected
		bool bIsConnected = false;

		FPlatformUserId PlatformUser = PLATFORMUSERID_NONE;
		FInputDeviceId InputDevice = INPUTDEVICEID_NONE;

		// Readable device name, cached at connect time so we don't re-query it (under a lock) every frame.
		FString DeviceName;

		// Diagnostic only: when this device last delivered an input report to the interface, and whether a
		// stall has already been reported for it. This distinguishes the two ways input can appear to "stop
		// working" -- the controller's reports no longer reaching us (a plugin/HID problem) versus reports
		// arriving fine but the engine not routing them (focus, input mode, bindings). Without it the two
		// look identical from the game.
		double LastReportTime = 0.0;
		bool bReportedInputStall = false;
		
		FJoyShockState SimpleState = {};
		FJoyShockState PreviousSimpleState = {};
		FIMUState IMUState = {};
		FIMUState PreviousIMUState = {};
		FTouchState TouchState = {};
		FTouchState PreviousTouchState = {};
		
		// Force-feedback values most recently set through Unreal's own system (Play Force Feedback Effect and
		// friends). Kept per device because SetChannelValue updates one channel at a time and the other three
		// have to survive that.
		FForceFeedbackValues ForceFeedback;
	};

	bool bIsGamepadAttached;
	const FName JoyShockControllerName = FName("JoyShock");

	static FString GetDeviceName(int32 InControllerId);

	// Controller states
	TMap<int32, FControllerState> ControllerStateByDeviceHandle = {};

	TArray<int32> DeviceHandles = {};

	// Guards structural access to DeviceHandles / ControllerStateByDeviceHandle. These containers are
	// read/iterated by the game thread (SendControllerEvents, connect/disconnect handling) and read by
	// the background polling threads, so all access must be serialised to avoid reallocation races.
	mutable FCriticalSection ControllerContainerLock;

	// Joy-Con joining: bidirectional map of a device handle to the handle it's joined with. A handle not
	// present here is a standalone controller.
	TMap<int32, int32> JoinPartner;

	// Player-slot assignment: maps each "logical controller" (a standalone device or a joined pair,
	// identified by its lower "primary" handle) to a player slot (0, 1, 2, ...). Slots are STABLE, not
	// compact: a controller keeps its slot for as long as it stays connected, and a disconnect leaves its
	// slot as a hole rather than shifting the others down, so nobody swaps characters mid-game. The hole is
	// reused by the next controller to connect. A game that wants a specific controller on a specific
	// player calls SetPlayerIndexForDevice -- connection order is otherwise the only thing deciding this,
	// and the plugin will not second-guess it. Recomputed by RefreshPlayerAssignments() whenever devices
	// connect/disconnect or joins change.
	TMap<int32, int32> PlayerSlotByPrimary;

	// Recomputes player slots and maps every connected device to its logical controller's platform user in
	// the platform input-device mapper, so the engine sees one player per logical controller.
	void RefreshPlayerAssignments();

	// Returns the primary (lower) handle of the logical controller this handle belongs to.
	int32 GetGroupPrimary(int32 Handle) const;

	// Every connected device currently driving the player Unreal identifies by InControllerId. Usually one,
	// but two for a joined Joy-Con pair -- both halves share a platform user, so force feedback aimed at
	// "that player" has to reach both. The caller must hold ControllerContainerLock.
	TArray<int32> GetDeviceHandlesForControllerId(int32 InControllerId) const;

	// Turns a device's stored force-feedback channels into a rumble request. The caller must hold
	// ControllerContainerLock.
	void SendForceFeedback(int32 DeviceHandle, const FForceFeedbackValues& Values) const;

	// Connect/disconnect notifications originate on background threads (enumeration and polling threads),
	// but touching the platform input-device mapper and our containers is only safe on the game thread.
	// We queue them here and drain them at the start of SendControllerEvents (which runs on the game thread).
	TQueue<int32, EQueueMode::Mpsc> PendingConnects;
	TQueue<TPair<int32, bool>, EQueueMode::Mpsc> PendingDisconnects;

	// Delay before sending a repeat message after a button was first pressed
	float InitialButtonRepeatDelay;

	// Delay before sending a repeat message after a button has been pressed for a while
	float ButtonRepeatDelay;

	TSharedRef<FGenericApplicationMessageHandler> MessageHandler;

	// Reports an axis to the engine as-is. Deadzones are deliberately not applied here -- see the note on
	// the definition.
	void OnControllerAnalog(const FPlatformUserId& InPlatformUser, const FInputDeviceId& InInputDevice,
						const FName& GamePadKey, float NewAxisValueNormalized, float OldAxisValueNormalized) const;
	
	void ProcessButtons(int32 CurrentButtons, int32 PreviousButtons, FPlatformUserId PlatformUser, FInputDeviceId InputDevice);
	void ProcessAnalogInputs(const FJoyShockState& SimpleState, const FJoyShockState& PreviousSimpleState, FPlatformUserId PlatformUser, FInputDeviceId InputDevice);
	// Callbacks
	void OnPollCallback(int32 DeviceHandle, const FJoyShockState& SimpleState, const FJoyShockState& PreviousSimpleState, const FIMUState& IMUState, const FIMUState& PreviousIMUState, float DeltaTime);

	void ProcessSingleTouchState(bool bTouchDown, int32 TouchID, const FVector2D& TouchLocation, bool bPreviousTouchDown, int32 PreviousTouchID, const FVector2D& PreviousTouchLocation, FPlatformUserId PlatformUser, FInputDeviceId InputDevice) const;
	void ProcessTouchState(const FTouchState& InTouchState, const FTouchState& InPreviousTouchState, FPlatformUserId PlatformUser, FInputDeviceId InputDevice) const;
	void OnTouchCallback(int32 DeviceHandle, const FTouchState& TouchState, const FTouchState& PreviousTouchState, float DeltaTime);
	// Reports this controller's motion to Unreal's motion input (Tilt / RotationRate / Gravity /
	// Acceleration), so gyro is bindable in Enhanced Input without any plugin-specific code.
	void ProcessIMUState(int32 DeviceHandle, const FIMUState& InIMUState, FPlatformUserId PlatformUser, FInputDeviceId InputDevice) const;

	void OnConnectCallback(int32 InDeviceHandle);

	void OnDisconnectCallback(int32 InDeviceHandle, bool bInHasTimedOut);

	// Additional input names.
	// JSL aliases one bit per pair of equivalent buttons across controller families, so each of these is a
	// single key covering both: PS == Home (JSMASK_PS == JSMASK_HOME) and TouchPadClick == Capture
	// (JSMASK_TOUCHPAD_CLICK == JSMASK_CAPTURE). That matches how Plus/Options and Minus/Share are already
	// handled (one abstract SpecialRight/SpecialLeft key), and how Unreal names shared positions.
	const FGamepadKeyNames::Type PSButtonKeyName = "PS";
	const FKey PSButtonKey = "PS";

	const FGamepadKeyNames::Type TouchPadClickKeyName = "TouchPadClick";
	const FKey TouchPadClickKey = "TouchPadClick";
	
	const FGamepadKeyNames::Type MicButtonKeyName = "Mic";
	const FKey MicButtonKey = "Mic";
	
	const FGamepadKeyNames::Type SideLeftButtonKeyName = "SideLeft";
	const FKey SideLeftButtonKey = "SideLeft";
	
	const FGamepadKeyNames::Type SideRightButtonKeyName = "SideRight";
	const FKey SideRightButtonKey = "SideRight";
	
	const FGamepadKeyNames::Type FunctionLeftButtonKeyName = "FunctionLeft";
	const FKey FunctionLeftButtonKey = "FunctionLeft";

	const FGamepadKeyNames::Type FunctionRightButtonKeyName = "FunctionRight";
	const FKey FunctionRightButtonKey = "FunctionRight";

	// Switch 2 Pro Controller
	const FGamepadKeyNames::Type CButtonKeyName = "SwitchC";
	const FKey CButtonKey = "SwitchC";

	const FGamepadKeyNames::Type GripLeftButtonKeyName = "GripLeft";
	const FKey GripLeftButtonKey = "GripLeft";

	const FGamepadKeyNames::Type GripRightButtonKeyName = "GripRight";
	const FKey GripRightButtonKey = "GripRight";
	
	
	const TArray<TTuple<int32, FName>> JoyShockMaskMappings = {
		{JSMASK_UP, FGamepadKeyNames::DPadUp},
		{JSMASK_DOWN, FGamepadKeyNames::DPadDown},
		{JSMASK_LEFT, FGamepadKeyNames::DPadLeft},
		{JSMASK_RIGHT, FGamepadKeyNames::DPadRight},
		{JSMASK_OPTIONS, FGamepadKeyNames::SpecialRight}, // Also matches JSMASK_PLUS
		{JSMASK_SHARE, FGamepadKeyNames::SpecialLeft}, // == JSMASK_MINUS
		{JSMASK_LCLICK, FGamepadKeyNames::LeftThumb},
		{JSMASK_RCLICK, FGamepadKeyNames::RightThumb},
		{JSMASK_L, FGamepadKeyNames::LeftShoulder},
		{JSMASK_R, FGamepadKeyNames::RightShoulder},
		{JSMASK_ZL, FGamepadKeyNames::LeftTriggerThreshold},
		{JSMASK_ZR, FGamepadKeyNames::RightTriggerThreshold},
		{JSMASK_S, FGamepadKeyNames::FaceButtonBottom},
		{JSMASK_E, FGamepadKeyNames::FaceButtonRight},
		{JSMASK_W, FGamepadKeyNames::FaceButtonLeft},
		{JSMASK_N, FGamepadKeyNames::FaceButtonTop},

		// These two masks are each shared by two names in JSL (PS == HOME, TOUCHPAD_CLICK == CAPTURE), so
		// there is one entry -- and one key -- per bit. Registering a separate Home/Capture key here would
		// be dead: the bit is already claimed, and nothing would ever emit it.
		{JSMASK_PS, PSButtonKeyName}, // == JSMASK_HOME
		{JSMASK_TOUCHPAD_CLICK, TouchPadClickKeyName}, // == JSMASK_CAPTURE
		{JSMASK_MIC, MicButtonKeyName},
		{JSMASK_SL, SideLeftButtonKeyName},
		{JSMASK_SR, SideRightButtonKeyName},
		{JSMASK_FNL, FunctionLeftButtonKeyName},
		{JSMASK_FNR, FunctionRightButtonKeyName},

		// Switch 2 Pro Controller
		{JSMASK_C, CButtonKeyName},
		{JSMASK_GL, GripLeftButtonKeyName},
		{JSMASK_GR, GripRightButtonKeyName}
	};

	TStaticArray<double, MAX_NUM_CONTROLLER_BUTTONS> NextRepeatTimes{InPlace, 0.0};

	UJoyShockLibrary4UnrealSettings* CachedSettings;

	FCriticalSection SimpleStateLock;
	FCriticalSection TouchStateLock;
}; 
