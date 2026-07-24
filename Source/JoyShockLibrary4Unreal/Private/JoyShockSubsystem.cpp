// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoyShockSubsystem.h"

#include "JoyShockLibrary4Unreal.h"
#include "JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

void UJoyShockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!FJoyShockLibrary4UnrealModule::IsAvailable())
	{
		// Without this the events would just never fire, with nothing to explain why.
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("UJoyShockSubsystem: the JoyShockLibrary4Unreal module is not loaded, so controller events will not fire."));
		return;
	}

	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	// Weak lambdas so a GC'd subsystem can never be broadcast into, even if Deinitialize is skipped.
	ConnectedHandle = JSL4UModule.GetOnDeviceConnected().AddWeakLambda(this, [this](int32 DeviceId)
	{
		const FJSL4UControllerInfo Info = UJoyShockLibrary::JSL4UGetControllerInfoAndSettings(DeviceId);
		LastControllerInfoByDeviceId.Add(DeviceId, Info);
		OnControllerConnected.Broadcast(Info);
	});

	DisconnectedHandle = JSL4UModule.GetOnDeviceDisconnected().AddWeakLambda(this, [this](int32 DeviceId, bool bTimedOut)
	{
		FJSL4UControllerInfo Info;
		if (const FJSL4UControllerInfo* CachedInfo = LastControllerInfoByDeviceId.Find(DeviceId))
		{
			Info = *CachedInfo;
		}
		else
		{
			Info.DeviceId = DeviceId;
		}
		Info.bIsConnected = false;
		OnControllerDisconnected.Broadcast(Info, bTimedOut);
		LastControllerInfoByDeviceId.Remove(DeviceId);
	});

	PairingChangedHandle = JSL4UModule.GetOnJoyConPairingChanged().AddWeakLambda(this,
		[this](int32 LeftDeviceId, int32 RightDeviceId, bool bJoined)
		{
			const FJSL4UControllerInfo LeftInfo =
				UJoyShockLibrary::JSL4UGetControllerInfoAndSettings(LeftDeviceId);
			const FJSL4UControllerInfo RightInfo =
				UJoyShockLibrary::JSL4UGetControllerInfoAndSettings(RightDeviceId);
			LastControllerInfoByDeviceId.Add(LeftDeviceId, LeftInfo);
			LastControllerInfoByDeviceId.Add(RightDeviceId, RightInfo);

			if (bJoined)
			{
				OnJoyConsJoined.Broadcast(LeftInfo, RightInfo);
			}
			else
			{
				OnJoyConsSeparated.Broadcast(LeftInfo, RightInfo);
			}
		});

	FunctionBlockedHandle = JSL4UModule.GetOnDeviceFunctionBlocked().AddWeakLambda(this,
		[this](int32 DeviceId, EJSL4UControllerFunction Function)
		{
			const FJSL4UControllerInfo Info = UJoyShockLibrary::JSL4UGetControllerInfoAndSettings(DeviceId);
			OnControllerFunctionBlocked.Broadcast(Info, Function);
		});

	// Controllers already connected when this subsystem is created never produced an event for it to hear,
	// so seed from JSL4UGetConnectedControllers on Init and use the events only for changes after that.
	const TArray<FJSL4UControllerInfo> ExistingControllers = UJoyShockLibrary::JSL4UGetConnectedControllers();
	for (const FJSL4UControllerInfo& Info : ExistingControllers)
	{
		LastControllerInfoByDeviceId.Add(Info.DeviceId, Info);
	}
	UE_LOG(LogJoyShockLibrary, Verbose, TEXT("UJoyShockSubsystem: listening for device changes (%d controller(s) already connected)."),
		ExistingControllers.Num());
}

void UJoyShockSubsystem::ListenForControllerInfo(FJSL4UControllerInfoConnectedSignature Event)
{
	if (!Event.IsBound())
	{
		return;
	}

	OnControllerConnected.AddUnique(Event);
	for (const FJSL4UControllerInfo& Info : UJoyShockLibrary::JSL4UGetConnectedControllers())
	{
		LastControllerInfoByDeviceId.Add(Info.DeviceId, Info);
		Event.Execute(Info);
	}
}

void UJoyShockSubsystem::ListenForControllerChanges(FJSL4UControllerInfoConnectedSignature OnConnected,
	FJSL4UControllerInfoDisconnectedSignature OnDisconnected)
{
	if (OnDisconnected.IsBound())
	{
		OnControllerDisconnected.AddUnique(OnDisconnected);
	}

	// Reuse the atomic bind-and-replay implementation so a connection cannot fall into a gap between
	// enumerating existing devices and subscribing for future ones.
	ListenForControllerInfo(OnConnected);
}

void UJoyShockSubsystem::ListenForJoyConPairingChanges(FJSL4UJoyConsPairingSignature OnJoined,
	FJSL4UJoyConsPairingSignature OnSeparated)
{
	if (OnJoined.IsBound())
	{
		OnJoyConsJoined.AddUnique(OnJoined);
	}
	if (OnSeparated.IsBound())
	{
		OnJoyConsSeparated.AddUnique(OnSeparated);
	}
}

APlayerController* UJoyShockSubsystem::FindLocalPlayerForController(int32 DeviceId) const
{
	const FJSL4UControllerInfo Info = UJoyShockLibrary::JSL4UGetControllerInfoAndSettings(DeviceId);
	const UGameInstance* GameInstance = GetGameInstance();
	if (!Info.bIsConnected || Info.PlatformUserId < 0 || GameInstance == nullptr)
	{
		return nullptr;
	}

	const FPlatformUserId PlatformUser = FPlatformUserId::CreateFromInternalId(Info.PlatformUserId);
	for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		if (LocalPlayer != nullptr && LocalPlayer->GetPlatformUserId() == PlatformUser)
		{
			return LocalPlayer->GetPlayerController(GetWorld());
		}
	}
	return nullptr;
}

bool UJoyShockSubsystem::EnsureLocalPlayerForController(int32 DeviceId, bool bCreateIfMissing,
	APlayerController*& PlayerController, int32& LocalPlayerIndex, bool& bWasCreated)
{
	PlayerController = nullptr;
	LocalPlayerIndex = INDEX_NONE;
	bWasCreated = false;

	const FJSL4UControllerInfo Info = UJoyShockLibrary::JSL4UGetControllerInfoAndSettings(DeviceId);
	UGameInstance* GameInstance = GetGameInstance();
	if (!Info.bIsConnected || Info.PlatformUserId < 0 || GameInstance == nullptr)
	{
		return false;
	}

	const FPlatformUserId PlatformUser = FPlatformUserId::CreateFromInternalId(Info.PlatformUserId);
	ULocalPlayer* LocalPlayer = nullptr;
	const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
	for (int32 Index = 0; Index < LocalPlayers.Num(); ++Index)
	{
		if (LocalPlayers[Index] != nullptr && LocalPlayers[Index]->GetPlatformUserId() == PlatformUser)
		{
			LocalPlayer = LocalPlayers[Index];
			LocalPlayerIndex = Index;
			break;
		}
	}

	if (LocalPlayer == nullptr && bCreateIfMissing)
	{
		FString Error;
		LocalPlayer = GameInstance->CreateLocalPlayer(PlatformUser, Error, true);
		if (LocalPlayer == nullptr)
		{
			UE_LOG(LogJoyShockLibrary, Warning,
				TEXT("EnsureLocalPlayerForController failed for device %d / platform user %d: %s"),
				DeviceId, Info.PlatformUserId, *Error);
			return false;
		}
		bWasCreated = true;
		LocalPlayerIndex = GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer);
	}

	if (LocalPlayer == nullptr || LocalPlayerIndex == INDEX_NONE)
	{
		return false;
	}

	// CreateLocalPlayer can remap an existing FInputDeviceId while synthesising its legacy controller id.
	// Assigning through the interface immediately reconciles every JoyShock device with the mapper again.
	if (!UJoyShockLibrary::JSL4UAssignControllerToPlayerIndex(DeviceId, LocalPlayerIndex))
	{
		return false;
	}

	PlayerController = LocalPlayer->GetPlayerController(GetWorld());
	return PlayerController != nullptr;
}

void UJoyShockSubsystem::StopAllRumble()
{
	// Setting the values is enough: the polling thread is the sole writer and sends one stop packet when it
	// sees them change to zero.
	for (const FJSL4UControllerInfo& Info : UJoyShockLibrary::JSL4UGetConnectedControllers())
	{
		UJoyShockLibrary::JSL4USetRumble(Info.DeviceId, 0.0f, 0.0f);
	}
}

void UJoyShockSubsystem::Deinitialize()
{
	// The module outlives the game instance, so leaving these bound would keep firing into a dead
	// subsystem for the rest of the session.
	if (FJoyShockLibrary4UnrealModule::IsAvailable())
	{
		FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
		JSL4UModule.GetOnDeviceConnected().Remove(ConnectedHandle);
		JSL4UModule.GetOnDeviceDisconnected().Remove(DisconnectedHandle);
		JSL4UModule.GetOnJoyConPairingChanged().Remove(PairingChangedHandle);
		JSL4UModule.GetOnDeviceFunctionBlocked().Remove(FunctionBlockedHandle);

		// Rumble is sustained by the polling threads, which belong to the module and keep running after the
		// game stops -- so a controller still rumbling when the game ends would rumble until something set
		// it back to zero, and nothing would. (This can't be left to the game: it may well be stopping
		// *because* it crashed.) Stopping in PIE is what makes this urgent; a packaged game exiting tears
		// the module down with it.
		StopAllRumble();
	}

	ConnectedHandle.Reset();
	DisconnectedHandle.Reset();
	PairingChangedHandle.Reset();
	FunctionBlockedHandle.Reset();
	LastControllerInfoByDeviceId.Reset();

	Super::Deinitialize();
}
