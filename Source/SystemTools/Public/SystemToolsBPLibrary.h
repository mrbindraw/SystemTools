// Copyright 2025 Andrew Bindraw. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/URLRequestFilter.h"
#include "Async/Async.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <psapi.h>
#include <tlhelp32.h>
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Windows/WindowsSystemIncludes.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "SystemToolsBPLibrary.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSystemTools, Log, All);

UCLASS()
class SYSTEMTOOLS_API USystemToolsBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

private:
	static void* BrowserWindowHandle;
	static TAtomic <uint32> ProcessId;
	static TAtomic <uint32> ProcessIdCached;
	static TAtomic <bool> IsBrowserChildProcess;

public:
	UFUNCTION(BlueprintCallable, Category = "SystemTools", DisplayName = "Launch URL Ex")
	static void LaunchURLEx(const FString& URL);

	UFUNCTION(BlueprintCallable, Category = "SystemTools", DisplayName = "Exec Command")
	static void ExecCommand(const FString& Command);

	virtual void BeginDestroy() override;
};
