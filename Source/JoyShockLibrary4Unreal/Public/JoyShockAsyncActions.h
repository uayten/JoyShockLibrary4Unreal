// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/CancellableAsyncAction.h"
#include "JoyShockSubsystem.h"
#include "JoyShockAsyncActions.generated.h"

/**
 * GAS-style async nodes for the plugin's events. Each of these places a single latent node (clock icon)
 * whose event pins carry their payload as regular data pins -- no Create Event / Bind Event / custom-event
 * signature matching, which is the part of the delegate workflow that is genuinely hard to wire correctly.
 *
 * The UJoyShockSubsystem Listen* functions and BlueprintAssignable events remain the underlying mechanism
 * (and stay available); these nodes are Blueprint sugar bound on top of the same subsystem, so both styles
 * see identical ordering and payloads. All pins fire on the game thread with player assignments already
 * applied, so the whole JSL4U* API is safe to call from them.
 *
 * Lifetime: the action lives until its owning Game Instance shuts down, or until Cancel is called on the
 * Async Action output pin. The pins can fire any number of times.
 */
UCLASS()
class JOYSHOCKLIBRARY4UNREAL_API UJSL4UWaitForControllerChanges : public UCancellableAsyncAction
{
	GENERATED_BODY()

public:
	// Fires once for every controller already connected when the node activates, then for every later
	// connection -- the same atomic bind-and-replay as Listen For Controllers, so no connection can fall
	// into a gap between placing the node and the controller arriving (in a packaged game controllers
	// usually finish enumerating after BeginPlay).
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "On Connected"))
	FJSL4UControllerInfoConnectedDelegate OnConnected;

	// Fires with the controller's last known identity; Timed Out is true when reports stopped instead of
	// a clean disconnect.
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "On Disconnected"))
	FJSL4UControllerInfoDisconnectedDelegate OnDisconnected;

	/**
	 * Waits for controller connections and disconnections. Controllers already connected fire On Connected
	 * immediately, so there is no window in which one can be missed.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Events",
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject",
			DisplayName = "Wait For Controller Changes",
			Keywords = "JSL4U listen controller connect disconnect joyshock"))
	static UJSL4UWaitForControllerChanges* WaitForControllerChanges(UObject* WorldContextObject);

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;

private:
	UFUNCTION()
	void HandleConnected(FJSL4UControllerInfo Controller);
	UFUNCTION()
	void HandleDisconnected(FJSL4UControllerInfo Controller, bool bTimedOut);

	TWeakObjectPtr<UJoyShockSubsystem> Subsystem;
};

UCLASS()
class JOYSHOCKLIBRARY4UNREAL_API UJSL4UWaitForJoyConPairingChanges : public UCancellableAsyncAction
{
	GENERATED_BODY()

public:
	// Fires after a left and right Joy-Con become one logical vertical controller. Both infos already
	// contain the shared player index and Vertical grip mode.
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "On Joined"))
	FJSL4UJoyConsPairingDelegate OnJoined;

	// Fires after a pair becomes two standalone horizontal controllers, each info already carrying its own
	// separate player assignment.
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "On Separated"))
	FJSL4UJoyConsPairingDelegate OnSeparated;

	/**
	 * Waits for Joy-Con join/separate transitions. No replay: these are actions, not persistent state --
	 * query Get Connected Controllers when the current pairing is also needed during setup.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Events",
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject",
			DisplayName = "Wait For Joy-Con Pairing Changes",
			Keywords = "JSL4U joycon join separate pair joyshock"))
	static UJSL4UWaitForJoyConPairingChanges* WaitForJoyConPairingChanges(UObject* WorldContextObject);

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;

private:
	UFUNCTION()
	void HandleJoined(FJSL4UControllerInfo LeftJoyCon, FJSL4UControllerInfo RightJoyCon);
	UFUNCTION()
	void HandleSeparated(FJSL4UControllerInfo LeftJoyCon, FJSL4UControllerInfo RightJoyCon);

	TWeakObjectPtr<UJoyShockSubsystem> Subsystem;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJSL4ULowBatteryDelegate,
	FJSL4UControllerInfo, Controller, EJSL4UBatteryLevel, Level);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJSL4UChargingChangedDelegate,
	FJSL4UControllerInfo, Controller, bool, bIsCharging);

UCLASS()
class JOYSHOCKLIBRARY4UNREAL_API UJSL4UWaitForBatteryChanges : public UCancellableAsyncAction
{
	GENERATED_BODY()

public:
	// Fires when a controller's charge falls to Warn At Level or below while it is not charging. Fires
	// once per episode: it re-arms when the controller charges past that level again, or is plugged in.
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "On Low Battery"))
	FJSL4ULowBatteryDelegate OnLowBattery;

	// Fires whenever a controller starts or stops drawing power. Plugging in a warned controller also
	// clears its warning, so a game can dismiss the low-battery UI from here without tracking state.
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "On Charging Changed"))
	FJSL4UChargingChangedDelegate OnChargingChanged;

	/**
	 * Watches every connected controller's charge.
	 *
	 * Battery moves over minutes, so this polls once a second rather than adding a per-report event path;
	 * a controller that is already low when the node activates is reported on the first poll.
	 *
	 * Controllers reporting Unknown (the Switch 2 Pro Controller does) never raise On Low Battery -- an
	 * absent reading is not a flat battery.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Events",
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject",
			DisplayName = "Wait For Battery Changes", AdvancedDisplay = "WarnAtLevel",
			Keywords = "JSL4U battery low charge charging power joyshock"))
	static UJSL4UWaitForBatteryChanges* WaitForBatteryChanges(UObject* WorldContextObject,
		EJSL4UBatteryLevel WarnAtLevel = EJSL4UBatteryLevel::Low);

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;

private:
	bool Poll(float DeltaTime);

	struct FBatteryWatch
	{
		EJSL4UBatteryLevel Level = EJSL4UBatteryLevel::Unknown;
		bool bIsCharging = false;
		bool bLowReported = false;
		bool bSeen = false;
	};

	EJSL4UBatteryLevel WarnLevel = EJSL4UBatteryLevel::Low;
	TMap<int32, FBatteryWatch> WatchByDeviceId;
	FTSTicker::FDelegateHandle TickerHandle;
};

UCLASS()
class JOYSHOCKLIBRARY4UNREAL_API UJSL4UWaitForControllerFunctionBlocked : public UCancellableAsyncAction
{
	GENERATED_BODY()

public:
	// Fires when a controller function's output is rejected while its input keeps flowing -- another
	// application (Steam Input, typically) holding the controller. Once per function per failure episode.
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "On Blocked"))
	FJSL4UControllerFunctionBlockedDelegate OnBlocked;

	/**
	 * Waits for a controller function (rumble, player LEDs, HOME light, motion configuration) to be
	 * blocked by another application. Fires only when the game actually uses the function.
	 */
	UFUNCTION(BlueprintCallable, Category = "JoyShock Library|Events",
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject",
			DisplayName = "Wait For Controller Function Blocked",
			Keywords = "JSL4U steam conflict blocked rumble joyshock"))
	static UJSL4UWaitForControllerFunctionBlocked* WaitForControllerFunctionBlocked(UObject* WorldContextObject);

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;

private:
	UFUNCTION()
	void HandleBlocked(FJSL4UControllerInfo Controller, EJSL4UControllerFunction Function);

	TWeakObjectPtr<UJoyShockSubsystem> Subsystem;
};
