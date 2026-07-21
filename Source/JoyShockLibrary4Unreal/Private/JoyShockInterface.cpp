// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoyShockInterface.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
// #include "Windows/WindowsApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GenericPlatform/InputDeviceRegistry.h"
#include "JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include <functional>

#include "JoyShockLibrary4Unreal.h"
#include "JoyShockLibrary4UnrealSettings.h"

#define LOCTEXT_NAMESPACE "JoyShockLibrary"

TSharedRef<FJoyShockInterface> FJoyShockInterface::Create(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	return MakeShareable(new FJoyShockInterface(InMessageHandler));
}


FJoyShockInterface::FJoyShockInterface(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	: MessageHandler(InMessageHandler)
{
	CachedSettings = GetMutableDefault<UJoyShockLibrary4UnrealSettings>();

#if WITH_EDITOR
	CachedSettings->GetOnSettingsChanged().AddLambda([this]
	{
		CachedSettings = GetMutableDefault<UJoyShockLibrary4UnrealSettings>();
	});
#endif

	InitializeAdditionalKeys();

	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	// Expose this interface to the Blueprint pairing API (UJoyShockLibrary reaches it via the module).
	JSL4UModule.SetActiveInterface(this);

	// TODO: Bind these without using lambda if possible
	JoyShockLockedBindLambda(JSL4UModule, GetOnConnected(), [this](int32 DeviceHandle)
	{
		// Enumeration runs on a background thread, so queue the connect and process it on the game thread
		// in SendControllerEvents (where the platform input-device mapper is safe to touch).
		PendingConnects.Enqueue(DeviceHandle);
	});

	JoyShockLockedBindLambda(JSL4UModule, GetOnDisconnected(), [this](int32 DeviceHandle, bool bHasTimedOut)
	{
		// This fires on a background polling thread. The platform input-device mapper and our
		// controller containers must only be touched on the game thread, so queue the disconnect
		// and let SendControllerEvents process it on the next tick.
		PendingDisconnects.Enqueue(TPair<int32, bool>(DeviceHandle, bHasTimedOut));
	});

	JoyShockLockedBindLambda(JSL4UModule, GetOnPoll(), [this](int32 DeviceHandle, const FJoyShockState& SimpleState, const FJoyShockState& PreviousSimpleState, const FIMUState& IMUState, const FIMUState& PreviousIMUState, float DeltaTime)
	{
		if (this)
			this->OnPollCallback(DeviceHandle, SimpleState, PreviousSimpleState, IMUState, PreviousIMUState, DeltaTime);
	});

	JoyShockLockedBindLambda(JSL4UModule, GetOnPollTouch(), [this](int32 DeviceHandle, const FTouchState& TouchState, const FTouchState& PreviousTouchState, float DeltaTime)
	{
		if (this)
			this->OnTouchCallback(DeviceHandle, TouchState, PreviousTouchState, DeltaTime);
	});
	
	bIsGamepadAttached = false;
	InitialButtonRepeatDelay = 0.2f;
	ButtonRepeatDelay = 0.1f;

	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("InitialButtonRepeatDelay"), InitialButtonRepeatDelay, GInputIni);
	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("ButtonRepeatDelay"), ButtonRepeatDelay, GInputIni);

	// Pick up any devices that were already enumerated before this interface (and its OnConnected binding)
	// existed -- e.g. a WM_DEVICECHANGE that fired between module startup and interface creation.
	TArray<int32> ExistingHandles;
	UJoyShockLibrary::JslGetConnectedDeviceHandles(ExistingHandles);
	for (int32 ExistingHandle : ExistingHandles)
	{
		PendingConnects.Enqueue(ExistingHandle);
	}

	// Enumerate controllers that were already connected before the engine started. WM_DEVICECHANGE only
	// fires on later plug/unplug events, so without this initial pass pre-connected controllers are never
	// discovered. The callbacks are bound above, so OnConnected will be queued for each device found.
	// This runs on a background thread (blocking HID I/O must not stall the game thread).
	JSL4UModule.RequestConnectDevices();
}

FJoyShockInterface::~FJoyShockInterface()
{
	if (FJoyShockLibrary4UnrealModule::IsAvailable())
	{
		FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
		if (JSL4UModule.GetActiveInterface() == this)
		{
			JSL4UModule.SetActiveInterface(nullptr);
		}
	}
}

void FJoyShockInterface::InitializeAdditionalKeys()
{
	EKeys::AddMenuCategoryDisplayInfo(JoyShockControllerName, LOCTEXT("JoyShockSubCategory", "JoyShock"), TEXT("GraphEditor.PadEvent_16x"));
	
	// One key per shared bit, named for both families it covers: JSL gives Home (Switch) and PS
	// (PlayStation) the same mask, as it does for Capture and TouchPad Click, so a key per brand is not
	// something the input can distinguish. Branch on FJSL4UControllerInfo::ControllerType if you need a
	// brand-specific button prompt.
	EKeys::AddKey(FKeyDetails(PSButtonKey, LOCTEXT("JoyShock_PS_Button", "JoyShock Home / PS Button"), FKeyDetails::GamepadKey, JoyShockControllerName));
	EKeys::AddKey(FKeyDetails(TouchPadClickKey, LOCTEXT("JoyShock_TouchPad_Click", "JoyShock Capture / TouchPad Click"), FKeyDetails::GamepadKey, JoyShockControllerName));

	// DualShock/DualSense
	EKeys::AddKey(FKeyDetails(MicButtonKey, LOCTEXT("JoyShock_Mic_Button", "JoyShock Mic Button"), FKeyDetails::GamepadKey, JoyShockControllerName));

	// Single Joy-con
	EKeys::AddKey(FKeyDetails(SideLeftButtonKey, LOCTEXT("JoyShock_Side_Left", "JoyShock Side Left"), FKeyDetails::GamepadKey, JoyShockControllerName));
	EKeys::AddKey(FKeyDetails(SideRightButtonKey, LOCTEXT("JoyShock_Side_Right", "JoyShock Side Right"), FKeyDetails::GamepadKey, JoyShockControllerName));

	// DualSense Edge
	EKeys::AddKey(FKeyDetails(FunctionLeftButtonKey, LOCTEXT("JoyShock_Function_Left", "JoyShock Function Left"), FKeyDetails::GamepadKey, JoyShockControllerName));
	EKeys::AddKey(FKeyDetails(FunctionRightButtonKey, LOCTEXT("JoyShock_Function_Right", "JoyShock Function Right"), FKeyDetails::GamepadKey, JoyShockControllerName));

	// Switch 2 Pro Controller
	EKeys::AddKey(FKeyDetails(CButtonKey, LOCTEXT("JoyShock_Switch_C", "JoyShock C Button (Switch 2)"), FKeyDetails::GamepadKey, JoyShockControllerName));
	EKeys::AddKey(FKeyDetails(GripLeftButtonKey, LOCTEXT("JoyShock_Grip_Left", "JoyShock Grip Left GL (Switch 2)"), FKeyDetails::GamepadKey, JoyShockControllerName));
	EKeys::AddKey(FKeyDetails(GripRightButtonKey, LOCTEXT("JoyShock_Grip_Right", "JoyShock Grip Right GR (Switch 2)"), FKeyDetails::GamepadKey, JoyShockControllerName));
}

FString FJoyShockInterface::GetDeviceName(int32 InControllerId)
{
	// TODO: Add Player Number to Device Name
	int32 ControllerType = UJoyShockLibrary::JslGetControllerType(InControllerId);

	switch (ControllerType)
	{
	case JS_TYPE_JOYCON_LEFT:
		return TEXT("Left Joy-Con");
	case JS_TYPE_JOYCON_RIGHT:
		return TEXT("Right Joy-Con");
	case JS_TYPE_PRO_CONTROLLER:
		return TEXT("Pro Controller");
	case JS_TYPE_PRO_CONTROLLER_2:
		return TEXT("Pro Controller 2");
	case JS_TYPE_DS4:
		return TEXT("DualShock 4");
	case JS_TYPE_DS:
		return TEXT("DualSense");
	default:
		return TEXT("Unknown Controller");
	}
}

void FJoyShockInterface::SendControllerEvents()
{
	// Drain connects/disconnects queued from the background enumeration and polling threads. Handling
	// them here means the platform input-device mapper and our containers are only ever touched on the
	// game thread. Disconnects are processed before connects so that if a handle was freed and reused in
	// the same frame (reconnect), the device ends up connected rather than disconnected.
	{
		// Collected while draining and broadcast afterwards: a listener is free to call back into the
		// JSL4U* API, so we must not still be inside the callbacks (which take ControllerContainerLock)
		// when we fire, and the containers must already be consistent.
		TArray<TPair<int32, bool>> DisconnectedThisTick;
		TArray<int32> ConnectedThisTick;

		TPair<int32, bool> PendingDisconnect;
		while (PendingDisconnects.Dequeue(PendingDisconnect))
		{
			OnDisconnectCallback(PendingDisconnect.Key, PendingDisconnect.Value);
			DisconnectedThisTick.Add(PendingDisconnect);
		}

		int32 PendingConnect;
		while (PendingConnects.Dequeue(PendingConnect))
		{
			OnConnectCallback(PendingConnect);
			ConnectedThisTick.Add(PendingConnect);
		}

		if (DisconnectedThisTick.Num() > 0 || ConnectedThisTick.Num() > 0)
		{
			FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

			for (const TPair<int32, bool>& Disconnected : DisconnectedThisTick)
			{
				UE_LOG(LogJoyShockLibrary, Verbose, TEXT("Broadcasting disconnect of device %d (timed out: %d), %d listener(s)."),
					Disconnected.Key, Disconnected.Value ? 1 : 0, JSL4UModule.GetOnDeviceDisconnected().IsBound() ? 1 : 0);
				JSL4UModule.GetOnDeviceDisconnected().Broadcast(Disconnected.Key, Disconnected.Value);
			}
			for (int32 ConnectedDeviceId : ConnectedThisTick)
			{
				UE_LOG(LogJoyShockLibrary, Verbose, TEXT("Broadcasting connect of device %d, %d listener(s)."),
					ConnectedDeviceId, JSL4UModule.GetOnDeviceConnected().IsBound() ? 1 : 0);
				JSL4UModule.GetOnDeviceConnected().Broadcast(ConnectedDeviceId);
			}
		}
	}

	FScopeLock ContainerLock(&ControllerContainerLock);

	bIsGamepadAttached = !DeviceHandles.IsEmpty();

	for (int32 Index = DeviceHandles.Num() - 1; Index >= 0; Index--)
	{
		int32 DeviceHandle = DeviceHandles[Index];

		FControllerState& ControllerState = ControllerStateByDeviceHandle[DeviceHandle];
		if (ControllerState.bIsConnected)
		{
			// Diagnostic: a controller that is still "connected" but has stopped delivering reports is
			// invisible from the game -- input just goes quiet, exactly as if the engine had stopped routing
			// it. Say which of the two it is. (A healthy controller reports at 60Hz or faster, so a whole
			// second of silence is already far outside normal.)
			const double SecondsSinceLastReport = FPlatformTime::Seconds() - ControllerState.LastReportTime;
			if (SecondsSinceLastReport > 1.0)
			{
				if (!ControllerState.bReportedInputStall)
				{
					ControllerState.bReportedInputStall = true;
					UE_LOG(LogJoyShockLibrary, Warning,
						TEXT("Device %d (%s) is still connected but has not delivered an input report for %.1fs -- input has stalled below the engine, not in it."),
						DeviceHandle, *ControllerState.DeviceName, SecondsSinceLastReport);
				}
			}
			else if (ControllerState.bReportedInputStall)
			{
				ControllerState.bReportedInputStall = false;
				UE_LOG(LogJoyShockLibrary, Warning, TEXT("Device %d (%s) is delivering input reports again."),
					DeviceHandle, *ControllerState.DeviceName);
			}

			// if (CachedSettings->bControllerEventsWaitForEngineTick) // TODO: Implement this setting
			{
				// PlatformUser is the player slot this device is assigned to (see RefreshPlayerAssignments).
				// Both halves of a joined Joy-Con pair share the same PlatformUser, so their (disjoint)
				// buttons and separate stick axes combine into a single player.
				// (Device attribution for these events is registered once at connect time via
				// FInputDeviceRegistry, replacing the deprecated per-dispatch FInputDeviceScope.)
				const FPlatformUserId& PlatformUser = ControllerState.PlatformUser;
				const FInputDeviceId& InputDevice = ControllerState.InputDevice;

				FIMUState CurrentIMUState;
				{
					FScopeLock Lock(&SimpleStateLock);
					int32 CurrentButtons = ControllerState.SimpleState.buttons;
					int32 PreviousButtons = ControllerState.PreviousSimpleState.buttons;
					ProcessButtons(CurrentButtons, PreviousButtons, PlatformUser, InputDevice);
					ProcessAnalogInputs(ControllerState.SimpleState, ControllerState.PreviousSimpleState, PlatformUser, InputDevice);
					// Copied out rather than dispatched here: ProcessIMUState reads the motion state through
					// the Jsl* getters, and doing that under SimpleStateLock would nest this lock inside the
					// library's for no reason.
					CurrentIMUState = ControllerState.IMUState;
				}

				ProcessIMUState(DeviceHandle, CurrentIMUState, PlatformUser, InputDevice);
				
				{
					FScopeLock Lock(&TouchStateLock);
					ProcessTouchState(ControllerState.TouchState, ControllerState.PreviousTouchState, PlatformUser, InputDevice);
				}

				ControllerState.PreviousSimpleState = ControllerState.SimpleState;
				ControllerState.PreviousTouchState = ControllerState.TouchState;
				ControllerState.PreviousIMUState = ControllerState.IMUState;
			}
		}
		else
		{
			DeviceHandles.RemoveAt(Index);
		}
	}
}


void FJoyShockInterface::SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
{
	MessageHandler = InMessageHandler;
}

TArray<int32> FJoyShockInterface::GetDeviceHandlesForControllerId(int32 InControllerId) const
{
	// The id Unreal hands to force feedback is a player index, not one of our device handles, so it has to be
	// resolved back through the same mapper that RefreshPlayerAssignments assigns slots from. (The engine has
	// already undone the bOffsetPlayerGamepadIds offset before calling us, so this is the raw index.)
	TArray<int32> Result;
	if (InControllerId < 0)
	{
		return Result;
	}

	const FPlatformUserId User = IPlatformInputDeviceMapper::Get().GetPlatformUserForUserIndex(InControllerId);
	if (!User.IsValid())
	{
		return Result;
	}

	for (int32 Handle : DeviceHandles)
	{
		const FControllerState* State = ControllerStateByDeviceHandle.Find(Handle);
		if (State != nullptr && State->bIsConnected && State->PlatformUser == User)
		{
			Result.Add(Handle);
		}
	}
	return Result;
}

void FJoyShockInterface::SendForceFeedback(int32 DeviceHandle, const FForceFeedbackValues& Values) const
{
	// Match how Unreal's XInput interface reads these channels -- the large/low-frequency motor from
	// LeftLarge and the small/high-frequency one from RightSmall -- so that one Force Feedback Effect asset
	// feels the same on a JoyShock controller as it does on an Xbox pad. Picking a different pairing here
	// would make effects authored against a standard gamepad come out wrong on these controllers only.
	//
	// This only stores the values: the controller's own polling thread is the sole writer of rumble packets,
	// which is what keeps a blocking HID write off the game thread even while an effect is running every
	// frame.
	//
	// It writes to force feedback's own channel rather than through JSL4USetRumble, because Unreal calls in
	// here every frame -- with zeroes whenever no effect is playing. Sharing one pair of values meant that
	// per-frame zero wiped anything a game had set with JSL4USetRumble before the controller could act on
	// it, so direct rumble simply never happened. The polling thread takes the stronger of the two sources
	// per motor instead.
	UJoyShockLibrary::SetForceFeedbackRumble(DeviceHandle,
		FMath::RoundToInt(FMath::Clamp(Values.RightSmall, 0.0f, 1.0f) * 255.0f),
		FMath::RoundToInt(FMath::Clamp(Values.LeftLarge, 0.0f, 1.0f) * 255.0f));
}

void FJoyShockInterface::SetChannelValue(int32 ControllerId, const FForceFeedbackChannelType ChannelType, const float Value)
{
	FScopeLock ContainerLock(&ControllerContainerLock);

	for (int32 Handle : GetDeviceHandlesForControllerId(ControllerId))
	{
		FControllerState& ControllerState = ControllerStateByDeviceHandle[Handle];

		// One channel at a time, so the stored values carry the other three.
		switch (ChannelType)
		{
		case FForceFeedbackChannelType::LEFT_LARGE:
			ControllerState.ForceFeedback.LeftLarge = Value;
			break;
		case FForceFeedbackChannelType::LEFT_SMALL:
			ControllerState.ForceFeedback.LeftSmall = Value;
			break;
		case FForceFeedbackChannelType::RIGHT_LARGE:
			ControllerState.ForceFeedback.RightLarge = Value;
			break;
		case FForceFeedbackChannelType::RIGHT_SMALL:
			ControllerState.ForceFeedback.RightSmall = Value;
			break;
		}

		SendForceFeedback(Handle, ControllerState.ForceFeedback);
	}
}

void FJoyShockInterface::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values)
{
	FScopeLock ContainerLock(&ControllerContainerLock);

	for (int32 Handle : GetDeviceHandlesForControllerId(ControllerId))
	{
		FControllerState& ControllerState = ControllerStateByDeviceHandle[Handle];
		ControllerState.ForceFeedback = Values;
		SendForceFeedback(Handle, Values);
	}
}

void FJoyShockInterface::OnControllerAnalog(const FPlatformUserId& InPlatformUser, const FInputDeviceId& InInputDevice, const FName& GamePadKey, const float NewAxisValueNormalized, const float OldAxisValueNormalized) const
{
	// Axis values are reported raw. Deadzones belong to the game, not to the device: Enhanced Input has a
	// Dead Zone modifier per Input Action, which is per-player and tunable, and a device that filtered first
	// would destroy information no consumer could get back (gyro and motion work in particular want the raw
	// value). This used to carry a deadzone parameter fed from XInput's constants, gated behind a console
	// variable that never actually did anything -- the "or it changed" half of the test below matches on
	// every jittering sample, so nothing was ever filtered out.
	//
	// Keep sending while the axis is off centre, not only when it changes, so a stick held at a constant
	// deflection keeps feeding the engine; the change test is what delivers the final sample when it
	// returns to centre.
	if (NewAxisValueNormalized != 0.0f || OldAxisValueNormalized != NewAxisValueNormalized)
	{
		MessageHandler->OnControllerAnalog(GamePadKey, InPlatformUser, InInputDevice, NewAxisValueNormalized);
	}
}

void FJoyShockInterface::ProcessButtons(int32 CurrentButtons, int32 PreviousButtons, FPlatformUserId PlatformUser, FInputDeviceId InputDevice)
{
	const double CurrentTime = FPlatformTime::Seconds();

	int32 PressedButtons = ~PreviousButtons & CurrentButtons;
	int32 ReleasedButtons = PreviousButtons & ~CurrentButtons;
	int32 HeldButtons = CurrentButtons & PreviousButtons;
	
	for (int32 Index = 0; Index < JoyShockMaskMappings.Num(); Index++)
	{
		TTuple<int32, FName> MaskMappingTuple = JoyShockMaskMappings[Index];
		const int32& Mask = MaskMappingTuple.Key;
		const FName& Mapping = MaskMappingTuple.Value;

		if (PressedButtons & Mask)
		{
			// UE_LOG(LogJoyShockLibrary, Log, TEXT(">>>>>>>BUTTON PRESSED: %s"), *Mapping.ToString());
			MessageHandler->OnControllerButtonPressed(Mapping, PlatformUser, InputDevice, false);
					
			// this button was pressed - set the button's NextRepeatTime to the InitialButtonRepeatDelay
			NextRepeatTimes[Index] = CurrentTime + InitialButtonRepeatDelay;
		}
		else if (ReleasedButtons & Mask)
		{
			// UE_LOG(LogJoyShockLibrary, Log, TEXT(">>>>>>>BUTTON RELEASED: %s"), *Mapping.ToString());
			MessageHandler->OnControllerButtonReleased(Mapping, PlatformUser, InputDevice, false);
		}
		else if ((HeldButtons & Mask) && NextRepeatTimes[Index] <= CurrentTime)
		{
			// UE_LOG(LogJoyShockLibrary, Log, TEXT(">>>>>>>BUTTON HELD: %s"), *Mapping.ToString());
			MessageHandler->OnControllerButtonPressed(Mapping, PlatformUser, InputDevice, true);

			// set the button's NextRepeatTime to the ButtonRepeatDelay
			NextRepeatTimes[Index] = CurrentTime + ButtonRepeatDelay;
		}
	}
}

void FJoyShockInterface::ProcessAnalogInputs(const FJoyShockState& SimpleState, const FJoyShockState& PreviousSimpleState, FPlatformUserId PlatformUser, FInputDeviceId InputDevice)
{
	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::LeftAnalogX, SimpleState.stickLX, PreviousSimpleState.stickLX);
	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::LeftAnalogY, SimpleState.stickLY, PreviousSimpleState.stickLY);

	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::RightAnalogX, SimpleState.stickRX, PreviousSimpleState.stickRX);
	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::RightAnalogY, SimpleState.stickRY, PreviousSimpleState.stickRY);

	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::LeftTriggerAnalog, SimpleState.lTrigger, PreviousSimpleState.lTrigger);
	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::RightTriggerAnalog, SimpleState.rTrigger, PreviousSimpleState.rTrigger);
}

void FJoyShockInterface::ProcessIMUState(int32 DeviceHandle, const FIMUState& InIMUState, FPlatformUserId PlatformUser, FInputDeviceId InputDevice) const
{
	// Reports the controller's motion through Unreal's own motion input -- the same path a phone's gyro and
	// accelerometer use, feeding the Tilt / RotationRate / Gravity / Acceleration keys. That means gyro can
	// be bound in Enhanced Input like any other axis, instead of every project that wants motion having to
	// call this plugin's getters from Blueprint.
	//
	// The axes match JSL4UGetIMUState and JSL4UGetMotionState exactly, so a project can mix Enhanced Input
	// bindings and the direct getters without the two disagreeing about which way is up.
	const FVector RotationRate(-InIMUState.gyroZ, InIMUState.gyroX, -InIMUState.gyroY);
	const FVector Acceleration(InIMUState.accelZ, InIMUState.accelX, -InIMUState.accelY);

	// Gravity and orientation are derived state the library maintains, not part of the raw IMU sample, so
	// they come from the motion state rather than from InIMUState. get_motion_state only reads already
	// computed values, so this is cheap enough to do per controller per frame.
	const FJSL4UMotionState MotionState = UJoyShockLibrary::JSL4UGetMotionState(DeviceHandle);

	// Unreal's motion input expects Tilt as the device's attitude in radians. Euler() gives (Roll, Pitch,
	// Yaw) in degrees, which is the component order Unreal uses elsewhere for a rotation carried in a vector.
	const FVector EulerDegrees = MotionState.Orientation.Rotator().Euler();
	const FVector Tilt(
		FMath::DegreesToRadians(EulerDegrees.X),
		FMath::DegreesToRadians(EulerDegrees.Y),
		FMath::DegreesToRadians(EulerDegrees.Z));

	MessageHandler->OnMotionDetected(Tilt, RotationRate, MotionState.Gravity, Acceleration, PlatformUser, InputDevice);
}

void FJoyShockInterface::OnPollCallback(int32 DeviceHandle, const FJoyShockState& SimpleState, const FJoyShockState& PreviousSimpleState, const FIMUState& IMUState, const FIMUState& PreviousIMUState, float DeltaTime)
{
	// Runs on a background polling thread. Only read existing entries here (never add) so the map is not
	// structurally modified off the game thread; the entry is created by OnConnectCallback.
	FScopeLock ContainerLock(&ControllerContainerLock);

	if (!DeviceHandles.Contains(DeviceHandle))
		return;

	FControllerState* State = ControllerStateByDeviceHandle.Find(DeviceHandle);
	if (State == nullptr)
		return;

	State->LastReportTime = FPlatformTime::Seconds();

	// if (CachedSettings->bControllerEventsWaitForEngineTick) // TODO: Implement this setting
	{
		FScopeLock Lock(&SimpleStateLock);
		State->SimpleState.Update(SimpleState, State->PreviousSimpleState);
		State->IMUState = IMUState;
	}
	/*else
	{
		// Use cached data from OnConnectCallback
		const FPlatformUserId& PlatformUser = State.PlatformUser;
		const FInputDeviceId& InputDevice = State.InputDevice;

		ProcessAnalogInputs(SimpleState, PreviousSimpleState, PlatformUser, InputDevice);

		ProcessButtons(SimpleState.buttons, PreviousSimpleState.buttons, PlatformUser, InputDevice);

		// ProcessIMUState(IMUState, PreviousIMUState, PlatformUser, InputDevice);
	}*/
}

void FJoyShockInterface::ProcessSingleTouchState(bool bTouchDown, int32 TouchID, const FVector2D& TouchLocation, bool bPreviousTouchDown, int32 PreviousTouchID, const FVector2D& PreviousTouchLocation, FPlatformUserId PlatformUser, FInputDeviceId InputDevice) const
{
	if (bTouchDown && !bPreviousTouchDown)
	{
		// Just touched
		// UE_LOG(LogJoyShockLibrary, Log, TEXT(">>>>>Touch Started at %s with ID %d."), *TouchLocation.ToString(), TouchID);
		MessageHandler->OnTouchStarted(nullptr, TouchLocation, 1.0f, TouchID, PlatformUser, InputDevice);
	}
	else if (!bTouchDown && bPreviousTouchDown)
	{
		// Just released
		// UE_LOG(LogJoyShockLibrary, Log, TEXT(">>>>>Touch Released at %s with ID %d."), *TouchLocation.ToString(), TouchID);
		MessageHandler->OnTouchEnded(PreviousTouchLocation, TouchID, PlatformUser, InputDevice);
	}
	else if (bTouchDown && bPreviousTouchDown && TouchLocation != PreviousTouchLocation)
	{
		// Moved
		// UE_LOG(LogJoyShockLibrary, Log, TEXT(">>>>>Touch Moved at %s with ID %d."), *TouchLocation.ToString(), TouchID);
		MessageHandler->OnTouchMoved(TouchLocation, 1.0f, TouchID, PlatformUser, InputDevice);
	}
}

void FJoyShockInterface::ProcessTouchState(const FTouchState& InTouchState, const FTouchState& InPreviousTouchState, FPlatformUserId PlatformUser, FInputDeviceId InputDevice) const
{
	FVector2D CurrentTouch0Location(InTouchState.t0X, InTouchState.t0Y);
	FVector2D PreviousTouch0Location(InPreviousTouchState.t0X, InPreviousTouchState.t0Y);
	ProcessSingleTouchState(InTouchState.t0Down, /*InTouchState.t0Id*/ 0, CurrentTouch0Location, InPreviousTouchState.t0Down, InPreviousTouchState.t0Id, PreviousTouch0Location, PlatformUser, InputDevice);
    
	FVector2D CurrentTouch1Location(InTouchState.t1X, InTouchState.t1Y);
	FVector2D PreviousTouch1Location(InPreviousTouchState.t1X, InPreviousTouchState.t1Y);
	ProcessSingleTouchState(InTouchState.t1Down, /*InTouchState.t1Id*/ 1, CurrentTouch1Location, InPreviousTouchState.t1Down, InPreviousTouchState.t1Id, PreviousTouch1Location, PlatformUser, InputDevice);
}

void FJoyShockInterface::OnTouchCallback(int32 DeviceHandle, const FTouchState& TouchState, const FTouchState& PreviousTouchState, float DeltaTime)
{
	// Runs on a background polling thread. Only read existing entries here (never add) so the map is not
	// structurally modified off the game thread; the entry is created by OnConnectCallback.
	FScopeLock ContainerLock(&ControllerContainerLock);

	if (!DeviceHandles.Contains(DeviceHandle))
		return;

	FControllerState* ControllerState = ControllerStateByDeviceHandle.Find(DeviceHandle);
	if (ControllerState == nullptr)
		return;

	// if (CachedSettings->bControllerEventsWaitForEngineTick) // TODO: Implement this setting
	{
		FScopeLock Lock(&TouchStateLock);
		ControllerState->TouchState = TouchState;
	}
	/*else
	{
		const FPlatformUserId& PlatformUser = ControllerState.PlatformUser;
    	const FInputDeviceId& InputDevice = ControllerState.InputDevice;
    	
		ProcessTouchState(TouchState, PreviousTouchState, PlatformUser, InputDevice);
	}*/
}

void FJoyShockInterface::OnConnectCallback(int32 InDeviceHandle)
{
	// UE_LOG(LogJoyShockLibrary, Log, TEXT(">>>>>OnConnectCallback %d"), InDeviceHandle);
	FScopeLock ContainerLock(&ControllerContainerLock);

	// A device can be announced twice: it announces itself from its poll thread on first input, and this
	// interface also sweeps up already-connected devices when it is created (a device that announced before
	// the delegate was bound would otherwise be invisible). Handling the second one would allocate a second
	// input device id for the same controller and leave the first mapped as connected forever, so ignore it.
	if (const FControllerState* Existing = ControllerStateByDeviceHandle.Find(InDeviceHandle))
	{
		if (Existing->bIsConnected)
		{
			return;
		}
	}

	DeviceHandles.AddUnique(InDeviceHandle);

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();

	FControllerState& State = ControllerStateByDeviceHandle.FindOrAdd(InDeviceHandle);

	State.bIsConnected = true;
	// Seed the stall diagnostic from now, so the gap before the first report doesn't read as a stall.
	State.LastReportTime = FPlatformTime::Seconds();
	State.bReportedInputStall = false;

	// JSL reuses a freed device handle for the next controller to connect, so this entry may still hold the
	// previous occupant's buttons and axes. Clear them, or the first tick compares the new controller's
	// state against the old one's and fires phantom presses/releases.
	{
		FScopeLock StateLock(&SimpleStateLock);
		State.SimpleState = {};
		State.PreviousSimpleState = {};
		State.IMUState = {};
		State.PreviousIMUState = {};
	}
	{
		FScopeLock StateLock(&TouchStateLock);
		State.TouchState = {};
		State.PreviousTouchState = {};
	}

	// Allocate a globally-unique input device id for this physical controller. (Using the legacy
	// RemapControllerIdToPlatformUserAndDevice "best guess" device id here can collide with the keyboard's
	// device 0 / other controllers, which corrupts the input-device mapper and hangs Enhanced Input's
	// user-settings init when entering play with several controllers.)
	State.InputDevice = DeviceMapper.AllocateNewInputDeviceId();
	State.PlatformUser = PLATFORMUSERID_NONE; // assigned (and mapped connected) by RefreshPlayerAssignments
	State.DeviceName = GetDeviceName(InDeviceHandle);

	// Register the device's hardware descriptor once, for the lifetime of its device id (replaces the
	// deprecated per-dispatch FInputDeviceScope).
	FInputDeviceDescriptor Descriptor;
	Descriptor.HardwareDeviceHandle = State.InputDevice;
	Descriptor.InputDeviceName = TEXT("JoyShock4Unreal");
	Descriptor.HardwareDeviceIdentifier = FName(*State.DeviceName);
	FInputDeviceRegistry::RegisterDevice(Descriptor);

	RefreshPlayerAssignments();
}

void FJoyShockInterface::OnDisconnectCallback(int32 InDeviceHandle, bool bInHasTimedOut)
{
	// UE_LOG(LogJoyShockLibrary, Log, TEXT(">>>>>OnDisconnectCallback %d"), InDeviceHandle);
	FScopeLock ContainerLock(&ControllerContainerLock);

	if (!DeviceHandles.Contains(InDeviceHandle))
	{
		return; // Should never happen!
	}

	FControllerState& ControllerState = ControllerStateByDeviceHandle.FindChecked(InDeviceHandle);
	ControllerState.bIsConnected = false;

	// Tell the mapper this physical device is gone.
	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	DeviceMapper.Internal_MapInputDeviceToUser(ControllerState.InputDevice, ControllerState.PlatformUser, EInputDeviceConnectionState::Disconnected);

	// This device id is permanently retired (a reconnect allocates a fresh id), so drop its descriptor.
	FInputDeviceRegistry::RemoveDevice(ControllerState.InputDevice);

	// Dissolve any join this device was part of, then re-assign remaining players.
	JoinPartner.Remove(InDeviceHandle);
	for (auto It = JoinPartner.CreateIterator(); It; ++It)
	{
		if (It.Value() == InDeviceHandle)
		{
			It.RemoveCurrent();
		}
	}
	RefreshPlayerAssignments();
}

bool FJoyShockInterface::IsOwnInputDevice(FInputDeviceId InInputDevice) const
{
	for (const TTuple<int32, FControllerState>& Pair : ControllerStateByDeviceHandle)
	{
		if (Pair.Value.InputDevice == InInputDevice)
		{
			return true;
		}
	}
	return false;
}

bool FJoyShockInterface::IsPlayerSlotClaimedByAnotherDevice(int32 Slot) const
{
	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();

	const FPlatformUserId SlotUser = DeviceMapper.GetPlatformUserForUserIndex(Slot);
	if (!SlotUser.IsValid())
	{
		return false;
	}

	TArray<FInputDeviceId> DevicesOnSlot;
	DeviceMapper.GetAllConnectedInputDevicesForUser(SlotUser, DevicesOnSlot);

	const FInputDeviceId DefaultDevice = DeviceMapper.GetDefaultInputDevice();

	for (const FInputDeviceId& Device : DevicesOnSlot)
	{
		// The keyboard and mouse live on the first player and never stop a controller from going there --
		// otherwise no controller could ever be player 1.
		if (Device == DefaultDevice)
		{
			continue;
		}
		// Our own controllers are already accounted for by PlayerSlotByPrimary, and counting them here too
		// would push every controller after the first one slot further out than it should be.
		if (IsOwnInputDevice(Device))
		{
			continue;
		}
		return true;
	}

	return false;
}

int32 FJoyShockInterface::GetGroupPrimary(int32 Handle) const
{
	// The primary of a logical controller is the lower of the two joined handles (when both are connected),
	// otherwise the handle itself.
	if (const int32* Partner = JoinPartner.Find(Handle))
	{
		const FControllerState* PartnerState = ControllerStateByDeviceHandle.Find(*Partner);
		if (PartnerState != nullptr && PartnerState->bIsConnected)
		{
			return FMath::Min(Handle, *Partner);
		}
	}
	return Handle;
}

void FJoyShockInterface::RefreshPlayerAssignments()
{
	// Runs on the game thread with ControllerContainerLock held (via the connect/disconnect/join callers).
	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();

	// 1. Gather the primary handle of every connected logical controller (a standalone device or a pair).
	TArray<int32> GroupPrimaries;
	for (int32 Handle : DeviceHandles)
	{
		const FControllerState* State = ControllerStateByDeviceHandle.Find(Handle);
		if (State == nullptr || !State->bIsConnected)
		{
			continue;
		}
		GroupPrimaries.AddUnique(GetGroupPrimary(Handle));
	}

	// 2. Stable player slots: a controller keeps the same slot for its whole lifetime, so nobody's slot
	//    changes just because another controller connected or dropped. Release the slots of controllers
	//    that are gone, then give each newly-connected controller the lowest slot no one currently holds.
	//    This is what split-screen / party games (Overcooked-style) need: if player 0 drops mid-match,
	//    players 1 and 2 stay on their own characters instead of shuffling down onto each other's. The
	//    freed slot is left as a hole and reused by the next controller to connect. (The previous policy
	//    kept slots dense 0..N-1 but reassigned everyone on every disconnect, which is only ever right for
	//    a single grab-any-controller game and silently swaps characters for anything with more players.)

	// Release slots held by primaries that are no longer connected.
	for (auto It = PlayerSlotByPrimary.CreateIterator(); It; ++It)
	{
		if (!GroupPrimaries.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	// Assign the lowest free slot to each connected primary that doesn't have one yet, in handle order so
	// simultaneous connections are deterministic.
	TArray<int32> NewPrimaries;
	for (int32 Primary : GroupPrimaries)
	{
		if (!PlayerSlotByPrimary.Contains(Primary))
		{
			NewPrimaries.Add(Primary);
		}
	}
	NewPrimaries.Sort();

	for (int32 Primary : NewPrimaries)
	{
		int32 Slot = 0;
		for (bool bSlotTaken = true; bSlotTaken && Slot < MaxPlayerSlotSearch; )
		{
			bSlotTaken = false;
			for (const TTuple<int32, int32>& Pair : PlayerSlotByPrimary)
			{
				if (Pair.Value == Slot)
				{
					bSlotTaken = true;
					++Slot;
					break;
				}
			}

			// A slot is equally taken when someone else's controller is already on it -- an XInput pad, most
			// often. Without this the plugin only ever counted its own controllers, so the first JoyShock
			// controller claimed player 0 even with an Xbox pad already sitting there, and both drove the
			// same character. Skipping occupied slots is what makes a JoyShock controller land where a
			// second Xbox pad would have, so a game does not need one scheme for XInput and another for us.
			if (!bSlotTaken && IsPlayerSlotClaimedByAnotherDevice(Slot))
			{
				bSlotTaken = true;
				++Slot;
			}
		}
		PlayerSlotByPrimary.Add(Primary, Slot);
	}

	// 3. Map every connected device to its logical controller's player slot. Both halves of a joined pair
	//    map to the same platform user, so the engine sees one player for the pair.
	for (int32 Handle : DeviceHandles)
	{
		FControllerState* State = ControllerStateByDeviceHandle.Find(Handle);
		if (State == nullptr || !State->bIsConnected)
		{
			continue;
		}

		const int32 Slot = PlayerSlotByPrimary[GetGroupPrimary(Handle)];

		const FPlatformUserId SlotUser = DeviceMapper.GetPlatformUserForUserIndex(Slot);

		// Ask the mapper what it currently believes rather than trusting our own cache. The engine reassigns
		// devices behind our back: creating a local player runs the id through
		// RemapControllerIdToPlatformUserAndDevice, which synthesises an FInputDeviceId straight from the
		// controller id and claims it for the new player -- so "Create Player 1" quietly takes whichever of
		// our controllers happens to hold input device id 1. Routing follows the mapper, not the platform
		// user we pass with each event, so the stolen controller starts driving the wrong player while this
		// cache still says otherwise. Comparing against the cache meant we never noticed and never put it
		// back; comparing against the mapper repairs it on the next refresh.
		const FPlatformUserId MappedUser = DeviceMapper.GetUserForInputDevice(State->InputDevice);
		if (MappedUser != SlotUser)
		{
			UE_LOG(LogJoyShockLibrary, Verbose,
				TEXT("Device %d (input device %d) was mapped to platform user %d, restoring it to %d."),
				Handle, State->InputDevice.GetId(), MappedUser.GetInternalId(), SlotUser.GetInternalId());

			DeviceMapper.Internal_MapInputDeviceToUser(State->InputDevice, SlotUser, EInputDeviceConnectionState::Connected);
		}

		State->PlatformUser = SlotUser;
	}
}

bool FJoyShockInterface::JoinControllers(int32 HandleA, int32 HandleB)
{
	FScopeLock ContainerLock(&ControllerContainerLock);

	if (HandleA == HandleB)
	{
		return false;
	}

	const FControllerState* StateA = ControllerStateByDeviceHandle.Find(HandleA);
	const FControllerState* StateB = ControllerStateByDeviceHandle.Find(HandleB);
	if (StateA == nullptr || !StateA->bIsConnected || StateB == nullptr || !StateB->bIsConnected)
	{
		return false;
	}

	// Dissolve any existing joins the two handles are part of, then pair them. Joins are stored
	// bidirectionally, so removing a handle (as both key and value) fully dissolves its old pair.
	auto RemoveJoinsFor = [this](int32 Handle)
	{
		JoinPartner.Remove(Handle);
		for (auto It = JoinPartner.CreateIterator(); It; ++It)
		{
			if (It.Value() == Handle)
			{
				It.RemoveCurrent();
			}
		}
	};
	RemoveJoinsFor(HandleA);
	RemoveJoinsFor(HandleB);

	JoinPartner.Add(HandleA, HandleB);
	JoinPartner.Add(HandleB, HandleA);

	RefreshPlayerAssignments();
	return true;
}

void FJoyShockInterface::UnjoinController(int32 Handle)
{
	FScopeLock ContainerLock(&ControllerContainerLock);

	const int32* Partner = JoinPartner.Find(Handle);
	if (Partner == nullptr)
	{
		return;
	}

	JoinPartner.Remove(*Partner);
	JoinPartner.Remove(Handle);

	RefreshPlayerAssignments();
}

void FJoyShockInterface::UnjoinAllControllers()
{
	FScopeLock ContainerLock(&ControllerContainerLock);
	JoinPartner.Empty();
	RefreshPlayerAssignments();
}

int32 FJoyShockInterface::GetJoinPartner(int32 Handle) const
{
	FScopeLock ContainerLock(&ControllerContainerLock);
	const int32* Partner = JoinPartner.Find(Handle);
	return Partner != nullptr ? *Partner : INDEX_NONE;
}

bool FJoyShockInterface::SetPlayerIndexForDevice(int32 Handle, int32 PlayerIndex)
{
	FScopeLock ContainerLock(&ControllerContainerLock);

	const FControllerState* State = ControllerStateByDeviceHandle.Find(Handle);
	if (State == nullptr || !State->bIsConnected)
	{
		return false;
	}

	// Keyed on the group primary so assigning either half of a joined Joy-Con pair moves the pair.
	const int32 Primary = GetGroupPrimary(Handle);

	if (PlayerIndex < 0)
	{
		// Back to automatic: dropping the entry makes RefreshPlayerAssignments treat this as a controller
		// without a slot, so it takes the lowest one nobody holds.
		PlayerSlotByPrimary.Remove(Primary);
	}
	else
	{
		// Deliberately no check for the slot already being taken. Assignment is the game's call, and two
		// controllers sharing a player is a legitimate setup (it is exactly what a joined Joy-Con pair is),
		// so silently swapping or refusing here would be second-guessing the caller.
		PlayerSlotByPrimary.Add(Primary, PlayerIndex);
	}

	RefreshPlayerAssignments();
	return true;
}

int32 FJoyShockInterface::GetPlayerIndexForDevice(int32 Handle) const
{
	FScopeLock ContainerLock(&ControllerContainerLock);
	const FControllerState* State = ControllerStateByDeviceHandle.Find(Handle);
	if (State == nullptr || !State->bIsConnected)
	{
		return INDEX_NONE;
	}
	const int32* Slot = PlayerSlotByPrimary.Find(GetGroupPrimary(Handle));
	return Slot != nullptr ? *Slot : INDEX_NONE;
}
