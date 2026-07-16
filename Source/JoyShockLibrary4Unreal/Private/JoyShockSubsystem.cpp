// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoyShockSubsystem.h"

#include "JoyShockLibrary4Unreal.h"

void UJoyShockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!FJoyShockLibrary4UnrealModule::IsAvailable())
	{
		return;
	}

	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	// Weak lambdas so a GC'd subsystem can never be broadcast into, even if Deinitialize is skipped.
	ConnectedHandle = JSL4UModule.GetOnDeviceConnected().AddWeakLambda(this, [this](int32 DeviceId)
	{
		OnControllerConnected.Broadcast(DeviceId);
	});

	DisconnectedHandle = JSL4UModule.GetOnDeviceDisconnected().AddWeakLambda(this, [this](int32 DeviceId, bool bTimedOut)
	{
		OnControllerDisconnected.Broadcast(DeviceId, bTimedOut);
	});
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
	}

	ConnectedHandle.Reset();
	DisconnectedHandle.Reset();

	Super::Deinitialize();
}
