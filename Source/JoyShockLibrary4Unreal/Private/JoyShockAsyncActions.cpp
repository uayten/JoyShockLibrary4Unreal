// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoyShockAsyncActions.h"

#include "JoyShockLibrary4Unreal/JoyShockLibrary/JoyShockLibrary.h"
#include "Engine/GameInstance.h"

namespace
{
	// Shared activation plumbing: every action binds to the game instance's JoyShock subsystem, which is
	// the single source of truth for these events (it caches last-known infos for disconnects and already
	// receives everything on the game thread). An action outside a game world has nothing to bind to.
	UJoyShockSubsystem* ResolveSubsystem(const TWeakObjectPtr<UGameInstance>& RegisteredGameInstance)
	{
		UGameInstance* GameInstance = RegisteredGameInstance.Get();
		return GameInstance ? GameInstance->GetSubsystem<UJoyShockSubsystem>() : nullptr;
	}
}

// --- Wait For Controller Changes ---------------------------------------------------------------------

UJSL4UWaitForControllerChanges* UJSL4UWaitForControllerChanges::WaitForControllerChanges(UObject* WorldContextObject)
{
	UJSL4UWaitForControllerChanges* Action = NewObject<UJSL4UWaitForControllerChanges>();
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UJSL4UWaitForControllerChanges::Activate()
{
	UJoyShockSubsystem* FoundSubsystem = ResolveSubsystem(RegisteredWithGameInstance);
	if (FoundSubsystem == nullptr)
	{
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("Wait For Controller Changes: no JoyShock Subsystem reachable from this context; the node will never fire."));
		SetReadyToDestroy();
		return;
	}
	Subsystem = FoundSubsystem;

	FoundSubsystem->OnControllerDisconnected.AddDynamic(this, &UJSL4UWaitForControllerChanges::HandleDisconnected);

	// Bind first, then replay, mirroring Listen For Controllers: there is no gap in which a connection
	// arriving mid-activation can be missed, and a controller that connected long ago still fires.
	FoundSubsystem->OnControllerConnected.AddDynamic(this, &UJSL4UWaitForControllerChanges::HandleConnected);
	for (const FJSL4UControllerInfo& Info : UJoyShockLibrary::JSL4UGetConnectedControllers())
	{
		OnConnected.Broadcast(Info);
	}
}

void UJSL4UWaitForControllerChanges::SetReadyToDestroy()
{
	if (UJoyShockSubsystem* BoundSubsystem = Subsystem.Get())
	{
		BoundSubsystem->OnControllerConnected.RemoveAll(this);
		BoundSubsystem->OnControllerDisconnected.RemoveAll(this);
	}
	Super::SetReadyToDestroy();
}

void UJSL4UWaitForControllerChanges::HandleConnected(FJSL4UControllerInfo Controller)
{
	OnConnected.Broadcast(Controller);
}

void UJSL4UWaitForControllerChanges::HandleDisconnected(FJSL4UControllerInfo Controller, bool bTimedOut)
{
	OnDisconnected.Broadcast(Controller, bTimedOut);
}

// --- Wait For Joy-Con Pairing Changes ----------------------------------------------------------------

UJSL4UWaitForJoyConPairingChanges* UJSL4UWaitForJoyConPairingChanges::WaitForJoyConPairingChanges(UObject* WorldContextObject)
{
	UJSL4UWaitForJoyConPairingChanges* Action = NewObject<UJSL4UWaitForJoyConPairingChanges>();
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UJSL4UWaitForJoyConPairingChanges::Activate()
{
	UJoyShockSubsystem* FoundSubsystem = ResolveSubsystem(RegisteredWithGameInstance);
	if (FoundSubsystem == nullptr)
	{
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("Wait For Joy-Con Pairing Changes: no JoyShock Subsystem reachable from this context; the node will never fire."));
		SetReadyToDestroy();
		return;
	}
	Subsystem = FoundSubsystem;

	FoundSubsystem->OnJoyConsJoined.AddDynamic(this, &UJSL4UWaitForJoyConPairingChanges::HandleJoined);
	FoundSubsystem->OnJoyConsSeparated.AddDynamic(this, &UJSL4UWaitForJoyConPairingChanges::HandleSeparated);
}

void UJSL4UWaitForJoyConPairingChanges::SetReadyToDestroy()
{
	if (UJoyShockSubsystem* BoundSubsystem = Subsystem.Get())
	{
		BoundSubsystem->OnJoyConsJoined.RemoveAll(this);
		BoundSubsystem->OnJoyConsSeparated.RemoveAll(this);
	}
	Super::SetReadyToDestroy();
}

void UJSL4UWaitForJoyConPairingChanges::HandleJoined(FJSL4UControllerInfo LeftJoyCon, FJSL4UControllerInfo RightJoyCon)
{
	OnJoined.Broadcast(LeftJoyCon, RightJoyCon);
}

void UJSL4UWaitForJoyConPairingChanges::HandleSeparated(FJSL4UControllerInfo LeftJoyCon, FJSL4UControllerInfo RightJoyCon)
{
	OnSeparated.Broadcast(LeftJoyCon, RightJoyCon);
}

// --- Wait For Battery Changes ------------------------------------------------------------------------

UJSL4UWaitForBatteryChanges* UJSL4UWaitForBatteryChanges::WaitForBatteryChanges(UObject* WorldContextObject,
	EJSL4UBatteryLevel WarnAtLevel)
{
	UJSL4UWaitForBatteryChanges* Action = NewObject<UJSL4UWaitForBatteryChanges>();
	Action->WarnLevel = WarnAtLevel;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UJSL4UWaitForBatteryChanges::Activate()
{
	// Unlike the other actions here this one owns no subsystem binding: charge is not delivered as an
	// event, it is a value that changes over minutes, so a slow poll is both sufficient and far less
	// machinery than a per-report path from the polling threads.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UJSL4UWaitForBatteryChanges::Poll), 1.0f);

	// Report the current state immediately: a controller that was already low when this node activated is
	// exactly the case a warning exists for, and waiting a second to say so would be arbitrary.
	Poll(0.f);
}

void UJSL4UWaitForBatteryChanges::SetReadyToDestroy()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Super::SetReadyToDestroy();
}

bool UJSL4UWaitForBatteryChanges::Poll(float /*DeltaTime*/)
{
	const TArray<FJSL4UControllerInfo> Controllers = UJoyShockLibrary::JSL4UGetConnectedControllers();

	for (const FJSL4UControllerInfo& Info : Controllers)
	{
		FBatteryWatch& Watch = WatchByDeviceId.FindOrAdd(Info.DeviceId);
		const bool bFirstSight = !Watch.bSeen;
		const bool bChargingChanged = Watch.bIsCharging != Info.bIsCharging;

		Watch.bSeen = true;
		Watch.Level = Info.BatteryLevel;
		Watch.bIsCharging = Info.bIsCharging;

		// Charging is a state, so only transitions are worth reporting -- not the steady truth every
		// second. A controller already plugged in when the node activates is not an event.
		if (bChargingChanged && !bFirstSight)
		{
			OnChargingChanged.Broadcast(Info, Info.bIsCharging);
		}

		// Unknown is not a low battery: the Switch 2 Pro Controller reports it always, and every
		// controller reports it until its first input report arrives.
		const bool bIsLow = Info.BatteryLevel != EJSL4UBatteryLevel::Unknown
			&& Info.BatteryLevel <= WarnLevel
			&& !Info.bIsCharging;

		if (bIsLow)
		{
			if (!Watch.bLowReported)
			{
				Watch.bLowReported = true;
				OnLowBattery.Broadcast(Info, Info.BatteryLevel);
			}
		}
		else
		{
			// Re-arm once it climbs back above the threshold or goes on charge, so a controller that is
			// drained, charged and drained again warns each time rather than only once per session.
			Watch.bLowReported = false;
		}
	}

	// Forget controllers that have gone away, so a reused device id cannot inherit another controller's
	// "already warned" state.
	if (WatchByDeviceId.Num() > Controllers.Num())
	{
		TSet<int32> Live;
		Live.Reserve(Controllers.Num());
		for (const FJSL4UControllerInfo& Info : Controllers)
		{
			Live.Add(Info.DeviceId);
		}
		for (auto It = WatchByDeviceId.CreateIterator(); It; ++It)
		{
			if (!Live.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
	}

	return true; // keep ticking
}

// --- Wait For Controller Function Blocked ------------------------------------------------------------

UJSL4UWaitForControllerFunctionBlocked* UJSL4UWaitForControllerFunctionBlocked::WaitForControllerFunctionBlocked(UObject* WorldContextObject)
{
	UJSL4UWaitForControllerFunctionBlocked* Action = NewObject<UJSL4UWaitForControllerFunctionBlocked>();
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UJSL4UWaitForControllerFunctionBlocked::Activate()
{
	UJoyShockSubsystem* FoundSubsystem = ResolveSubsystem(RegisteredWithGameInstance);
	if (FoundSubsystem == nullptr)
	{
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("Wait For Controller Function Blocked: no JoyShock Subsystem reachable from this context; the node will never fire."));
		SetReadyToDestroy();
		return;
	}
	Subsystem = FoundSubsystem;

	FoundSubsystem->OnControllerFunctionBlocked.AddDynamic(this, &UJSL4UWaitForControllerFunctionBlocked::HandleBlocked);
}

void UJSL4UWaitForControllerFunctionBlocked::SetReadyToDestroy()
{
	if (UJoyShockSubsystem* BoundSubsystem = Subsystem.Get())
	{
		BoundSubsystem->OnControllerFunctionBlocked.RemoveAll(this);
	}
	Super::SetReadyToDestroy();
}

void UJSL4UWaitForControllerFunctionBlocked::HandleBlocked(FJSL4UControllerInfo Controller, EJSL4UControllerFunction Function)
{
	OnBlocked.Broadcast(Controller, Function);
}
