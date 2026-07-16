// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "JoyShockSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FJSL4UControllerConnectedDelegate, int32, DeviceId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FJSL4UControllerDisconnectedDelegate, int32, DeviceId, bool, bTimedOut);

/**
 * Blueprint access to controller connect/disconnect events, e.g. to keep a controller-assignment screen
 * up to date without polling. Both events fire on the game thread once the controller has been fully
 * added to / removed from the input device interface, so the whole JSL4U* API is safe to call from them.
 */
UCLASS(DisplayName = "JoyShock Subsystem")
class JOYSHOCKLIBRARY4UNREAL_API UJoyShockSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// A controller was connected. DeviceId is the handle every other JSL4U* / Jsl* node takes.
	UPROPERTY(BlueprintAssignable, Category = "JoyShockLibrary|Events")
	FJSL4UControllerConnectedDelegate OnControllerConnected;

	// A controller was disconnected. By the time this fires, any Joy-Con join it was part of has been
	// dissolved and the remaining player slots recompacted, so JSL4UGetConnectedControllers already
	// reports the new layout. bTimedOut is true when the controller stopped responding rather than
	// disconnecting cleanly.
	UPROPERTY(BlueprintAssignable, Category = "JoyShockLibrary|Events")
	FJSL4UControllerDisconnectedDelegate OnControllerDisconnected;

	// Stops rumble on every connected controller. Called automatically when the game shuts down -- rumble
	// is sustained by threads that outlive the game, so a controller left rumbling would never stop on its
	// own. Also useful on its own (pausing, a cutscene, losing focus).
	UFUNCTION(BlueprintCallable, Category = "JoyShockLibrary|Rumble")
	void StopAllRumble();

private:
	FDelegateHandle ConnectedHandle;
	FDelegateHandle DisconnectedHandle;
};
