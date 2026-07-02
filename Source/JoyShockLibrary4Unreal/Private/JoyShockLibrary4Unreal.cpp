// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoyShockLibrary4Unreal.h"

#include "JoyShockInterface.h"
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
				UJoyShockLibrary::JslConnectDevices();
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

#if PLATFORM_WINDOWS
	if (hidapiDllHandle)
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