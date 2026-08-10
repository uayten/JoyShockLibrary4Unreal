// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoyShockLibrary4Unreal.h"
#include "JoyShockBlueprintLibrary.h"

#include "JoyShockInterface.h"
#include "JoyShockLibrary4Unreal/JoyShockLibrary/Switch2Bluetooth.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FJoyShockLibrary4UnrealModule"

#if PLATFORM_WINDOWS
void *hidapiDllHandle = nullptr;

bool FWindowsDeviceChangeMessageHandler::ProcessMessage(HWND hwnd, uint32 msg, WPARAM wParam, LPARAM lParam,
	int32& OutResult)
{
	if (msg == WM_DEVICECHANGE)
	{
		// UE_LOG(LogTemp, Log, TEXT(">>>>>CONNECTING DEVICES TO JoyShockLibrary"));
		// Never enumerate on the game thread (this runs inside the game-thread message pump) -- HID
		// enumeration/initialisation blocks and would freeze the editor. Hand it to a background thread.
		FJoyShockLibrary4UnrealModule::GetInstance().RequestConnectDevices();
	}

	return false;
}
#endif

void FJoyShockLibrary4UnrealModule::RequestConnectDevices()
{
	if (bShuttingDown.load())
	{
		return;
	}

	// Mark that an enumeration pass is wanted. If a worker is already running it will pick this up.
	bConnectQueued.store(true);

	bool bExpected = false;
	if (!bConnectRunning.compare_exchange_strong(bExpected, true))
	{
		return; // A worker is already running; it will observe bConnectQueued.
	}

	Async(EAsyncExecution::Thread, [this]()
	{
		for (;;)
		{
			// Coalesce every request that arrived since we started into this single pass.
			while (bConnectQueued.exchange(false))
			{
				if (bShuttingDown.load())
				{
					break;
				}
				UJoyShockLibrary::ConnectDevices();
			}

			bConnectRunning.store(false);

			// A request may have arrived between our last exchange(false) and clearing bConnectRunning;
			// if so, try to re-acquire ownership, otherwise a later RequestConnectDevices() will.
			if (!bConnectQueued.load() || bShuttingDown.load())
			{
				break;
			}

			bool bReExpected = false;
			if (!bConnectRunning.compare_exchange_strong(bReExpected, true))
			{
				break; // Someone else took ownership.
			}
		}
	});
}

void FJoyShockLibrary4UnrealModule::StartupModule()
{
	IInputDeviceModule::StartupModule();

	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<GenericApplication> Application = FSlateApplication::Get().GetPlatformApplication();

		if (Application.IsValid())
		{
#if PLATFORM_WINDOWS
			FWindowsApplication* WindowsApplication = static_cast<FWindowsApplication*>(Application.Get());
			WindowsApplication->AddMessageHandler(WindowsDeviceChangeMessageHandler);
#endif
			// TODO: Add Linux and macOS support
		}
	}

#if PLATFORM_WINDOWS
	FString AbsPath = IPluginManager::Get().FindPlugin("JoyShockLibrary4Unreal")->GetBaseDir() / TEXT("ThirdParty/hidapi/x64");
	hidapiDllHandle = FPlatformProcess::GetDllHandle(*(AbsPath / TEXT("hidapi.dll")));
#endif

	// Start listening for Switch 2 controllers on the radio. A controller advertising is the Bluetooth
	// counterpart of a USB device being plugged in, and the message handler above never hears about it --
	// WM_DEVICECHANGE is for devices Windows enumerates, and these are not among them. So the scan drives
	// the same coalesced enumeration pass directly.
	//
	// On a background thread because querying the adapter blocks, and this runs during engine startup.
	Async(EAsyncExecution::Thread, [this]()
	{
		if (bShuttingDown.load() || !Switch2Ble::IsSupported())
		{
			return;
		}

		Switch2Ble::SetDiscoveryCallback([]()
		{
			// Guarded because this runs on a radio thread, which knows nothing about the engine's lifetime:
			// an advertisement can arrive at any moment, including after the module has gone. GetInstance
			// would load the module back in to answer that.
			if (FJoyShockLibrary4UnrealModule::IsAvailable())
			{
				FJoyShockLibrary4UnrealModule::GetInstance().RequestConnectDevices();
			}
		});
		Switch2Ble::StartScan();
	});
}

void FJoyShockLibrary4UnrealModule::ShutdownModule()
{
	// Stop accepting new enumeration requests and wait for any in-flight background pass to finish
	// before we free the hidapi DLL out from under it.
	bShuttingDown.store(true);
	while (bConnectRunning.load())
	{
		FPlatformProcess::Sleep(0.001f);
	}

	// Then stop the controllers themselves, before anything they use goes away. Their polling threads own a
	// hidapi handle or a Bluetooth connection each, and call back into this module; leaving them running
	// while the rest of this function frees those was the crash on closing the editor with a controller on
	// the radio, because the Bluetooth teardown below freed the very object each thread was asleep inside.
	const bool bAllDevicesStopped = ShutdownAllDevices();

	// Now the radio: stop scanning, drop the discovery callback into this module, and release any connection
	// no controller claimed. By this point every controller has handed its own connection back, so this is
	// normally just the scan. Unlike the HID devices these hold WinRT objects whose callbacks fire on their
	// own threads, so they have to be torn down before the module goes.
	Switch2Ble::Shutdown();

#if PLATFORM_WINDOWS
	// Only once nothing can still be inside it. A polling thread that outran the wait above is still holding
	// a hidapi handle, and unloading the library under it would turn a leak into an access violation on the
	// way out -- so the leak stands, and the process reclaims it a moment later.
	if (hidapiDllHandle && bAllDevicesStopped)
	{
		FPlatformProcess::FreeDllHandle(hidapiDllHandle);
		hidapiDllHandle = nullptr;
	}
#endif

	// Application is already invalid at this point, so there's no message handler to remove.
	/*if (FSlateApplication::IsInitialized())
	{
		UE_LOG(LogTemp, Log, TEXT(">>>>>SHUTTING DOWN JoyShockLibrary MODULE"));
		if (TSharedPtr<GenericApplication> Application = FSlateApplication::Get().GetPlatformApplication())
		{
			if (Application.IsValid())
			{
#if PLATFORM_WINDOWS
				FWindowsApplication* WindowsApplication = static_cast<FWindowsApplication*>(Application.Get());
				WindowsApplication->RemoveMessageHandler(WindowsDeviceChangeMessageHandler);
#endif
			}
		}
	}*/

	IInputDeviceModule::ShutdownModule();
}

TSharedPtr<IInputDevice> FJoyShockLibrary4UnrealModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	return FJoyShockInterface::Create(InMessageHandler);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJoyShockLibrary4UnrealModule, JoyShockLibrary4Unreal)