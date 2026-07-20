// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoyShockSubsystem.h"

#include "JoyShockLibrary4Unreal.h"
#include "JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockLibrary.h"

void UJoyShockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!FJoyShockLibrary4UnrealModule::IsAvailable())
	{
		// Without this the events would just never fire, with nothing to explain why.
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("UJoyShockSubsystem: the JoyShockLibrary4Unreal module is not loaded, so OnControllerConnected / OnControllerDisconnected will never fire."));
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

	// Controllers already connected when this subsystem is created never produced an event for it to hear,
	// so seed from JSL4UGetConnectedControllers on Init and use the events only for changes after that.
	UE_LOG(LogJoyShockLibrary, Verbose, TEXT("UJoyShockSubsystem: listening for device changes (%d controller(s) already connected)."),
		UJoyShockLibrary::JSL4UGetConnectedControllers().Num());
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

		// Rumble is sustained by the polling threads, which belong to the module and keep running after the
		// game stops -- so a controller still rumbling when the game ends would rumble until something set
		// it back to zero, and nothing would. (This can't be left to the game: it may well be stopping
		// *because* it crashed.) Stopping in PIE is what makes this urgent; a packaged game exiting tears
		// the module down with it.
		StopAllRumble();
	}

	ConnectedHandle.Reset();
	DisconnectedHandle.Reset();

	Super::Deinitialize();
}
