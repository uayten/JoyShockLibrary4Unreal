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

static int32 JoyShockEnableXInputDeadzones = 0;
FAutoConsoleVariableRef CVarJoyShockLeftStickMessageDeadzone
(
	TEXT("JoyShock.JoyShockEnableXInputDeadzones"),
	JoyShockEnableXInputDeadzones,
	TEXT("Enable the same deadzone values for triggers and analog sticks used by Unreal's XInput interface. If disabled, no deadzones will be used.\n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default
);

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
	bNeedsControllerStateUpdate = true;
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
	
	EKeys::AddKey(FKeyDetails(HomeButtonKey, LOCTEXT("JoyShock_Home_Button", "JoyShock Home Button"), FKeyDetails::GamepadKey, JoyShockControllerName));

	// DualShock/DualSense
	EKeys::AddKey(FKeyDetails(PSButtonKey, LOCTEXT("JoyShock_PS_Button", "JoyShock PS Button"), FKeyDetails::GamepadKey, JoyShockControllerName));

	// Switch
	EKeys::AddKey(FKeyDetails(CaptureButtonKey, LOCTEXT("JoyShock_Capture", "JoyShock Capture"), FKeyDetails::GamepadKey, JoyShockControllerName));

	// DualShock/DualSense
	EKeys::AddKey(FKeyDetails(TouchPadClickKey, LOCTEXT("JoyShock_TouchPad_Click", "JoyShock TouchPad Click"), FKeyDetails::GamepadKey, JoyShockControllerName));
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
		TPair<int32, bool> PendingDisconnect;
		while (PendingDisconnects.Dequeue(PendingDisconnect))
		{
			OnDisconnectCallback(PendingDisconnect.Key, PendingDisconnect.Value);
		}

		int32 PendingConnect;
		while (PendingConnects.Dequeue(PendingConnect))
		{
			OnConnectCallback(PendingConnect);
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
			// if (CachedSettings->bControllerEventsWaitForEngineTick) // TODO: Implement this setting
			{
				// PlatformUser is the player slot this device is assigned to (see RefreshPlayerAssignments).
				// Both halves of a joined Joy-Con pair share the same PlatformUser, so their (disjoint)
				// buttons and separate stick axes combine into a single player.
				// (Device attribution for these events is registered once at connect time via
				// FInputDeviceRegistry, replacing the deprecated per-dispatch FInputDeviceScope.)
				const FPlatformUserId& PlatformUser = ControllerState.PlatformUser;
				const FInputDeviceId& InputDevice = ControllerState.InputDevice;

				{
					FScopeLock Lock(&SimpleStateLock);
					int32 CurrentButtons = ControllerState.SimpleState.buttons;
					int32 PreviousButtons = ControllerState.PreviousSimpleState.buttons;
					ProcessButtons(CurrentButtons, PreviousButtons, PlatformUser, InputDevice);
					ProcessAnalogInputs(ControllerState.SimpleState, ControllerState.PreviousSimpleState, PlatformUser, InputDevice);
					// ProcessIMUState(ControllerState.IMUState, ControllerState.PreviousIMUState, PlatformUser, InputDevice);
				}
				
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

void FJoyShockInterface::SetChannelValue(int32 ControllerId, const FForceFeedbackChannelType ChannelType, const float Value)
{
	if (ControllerId >= 0 && ControllerId < MAX_NUM_JOYSHOCK_CONTROLLERS)
	{
		// TODO: Implement rumble
		/*FControllerState& ControllerState = ControllerStates[ ControllerId ];

		if( ControllerState.bIsConnected )
		{
			switch( ChannelType )
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
		}*/
	}
}

void FJoyShockInterface::SetChannelValues( int32 ControllerId, const FForceFeedbackValues &Values )
{
	if (ControllerId >= 0 && ControllerId < MAX_NUM_JOYSHOCK_CONTROLLERS)
	{
		// TODO: Implement rumble
		/*FControllerState& ControllerState = ControllerStates[ ControllerId ];

		if( ControllerState.bIsConnected )
		{
			ControllerState.ForceFeedback = Values;
		}*/
	}
}

void FJoyShockInterface::OnControllerAnalog(const FPlatformUserId& InPlatformUser, const FInputDeviceId& InInputDevice, const FName& GamePadKey, const float NewAxisValueNormalized, const float OldAxisValueNormalized, float DeadZone) const
{
	if (JoyShockEnableXInputDeadzones == 0)
		DeadZone = 0.0f;

	// Send new analog data if it's different or outside the platform deadzone.
	if (OldAxisValueNormalized != NewAxisValueNormalized || FMath::Abs(NewAxisValueNormalized) > DeadZone)
		MessageHandler->OnControllerAnalog(GamePadKey, InPlatformUser, InInputDevice, NewAxisValueNormalized);
	
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
	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::LeftAnalogX, SimpleState.stickLX, PreviousSimpleState.stickLX, XInputLeftStickDeadzone);
	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::LeftAnalogY, SimpleState.stickLY, PreviousSimpleState.stickLY, XInputLeftStickDeadzone);

	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::RightAnalogX, SimpleState.stickRX, PreviousSimpleState.stickRX, XInputRightStickDeadzone);
	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::RightAnalogY, SimpleState.stickRY, PreviousSimpleState.stickRY, XInputRightStickDeadzone);

	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::LeftTriggerAnalog, SimpleState.lTrigger, PreviousSimpleState.lTrigger, XInputTriggerDeadzone);
	OnControllerAnalog(PlatformUser, InputDevice, FGamepadKeyNames::RightTriggerAnalog, SimpleState.rTrigger, PreviousSimpleState.rTrigger, XInputTriggerDeadzone);
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

	DeviceHandles.AddUnique(InDeviceHandle);

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();

	FControllerState& State = ControllerStateByDeviceHandle.FindOrAdd(InDeviceHandle);

	State.bIsConnected = true;
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

	// 2. Assign dense player slots (0..N-1). Groups that already had a slot keep their relative order;
	//    new groups go after them (by handle). Compacting means that when a lower-numbered player
	//    disconnects, the remaining controllers shift down -- e.g. the last controller left always becomes
	//    player 0. (Trade-off: if player 0 drops mid-match, player 1 becomes player 0.)
	TArray<int32> OrderedPrimaries = GroupPrimaries;
	OrderedPrimaries.Sort([this](const int32& A, const int32& B)
	{
		const int32* SlotA = PlayerSlotByPrimary.Find(A);
		const int32* SlotB = PlayerSlotByPrimary.Find(B);
		const int32 KeyA = SlotA != nullptr ? *SlotA : MAX_int32;
		const int32 KeyB = SlotB != nullptr ? *SlotB : MAX_int32;
		return KeyA != KeyB ? KeyA < KeyB : A < B;
	});

	TMap<int32, int32> NewSlotByPrimary;
	for (int32 Index = 0; Index < OrderedPrimaries.Num(); Index++)
	{
		NewSlotByPrimary.Add(OrderedPrimaries[Index], Index);
	}
	PlayerSlotByPrimary = MoveTemp(NewSlotByPrimary);

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

		if (State->PlatformUser != SlotUser)
		{
			DeviceMapper.Internal_MapInputDeviceToUser(State->InputDevice, SlotUser, EInputDeviceConnectionState::Connected);
			State->PlatformUser = SlotUser;
		}
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
