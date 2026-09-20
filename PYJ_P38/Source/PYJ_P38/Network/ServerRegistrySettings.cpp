// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServerRegistrySettings.h"

#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	/** Returns the command-line value for -Key=Value, or Fallback when the switch is absent. */
	FString ValueOrCommandLine(const FString& Fallback, const TCHAR* Switch)
	{
		FString Override;
		if (FParse::Value(FCommandLine::Get(), Switch, Override) && !Override.IsEmpty())
		{
			return Override;
		}
		return Fallback;
	}
}

UServerRegistrySettings::UServerRegistrySettings()
	: ApiBaseUrl(TEXT("http://127.0.0.1:8080"))
	, ServerApiKey(TEXT("dev-server-key"))
	, RequestTimeoutSeconds(10.0f)
	, bAutoRegisterOnDedicatedServer(true)
	, ServerName(TEXT(""))
	, Region(TEXT("KR"))
	, MaxPlayers(32)
	, HeartbeatIntervalSeconds(10.0f)
	, PublicIpOverride(TEXT(""))
	, bAutoLoginOnClientStart(false)
	, AutoLoginUsername(TEXT(""))
	, AutoLoginPassword(TEXT(""))
	, bAutoJoinAfterLogin(true)
{
}

const UServerRegistrySettings& UServerRegistrySettings::Get()
{
	const UServerRegistrySettings* Settings = GetDefault<UServerRegistrySettings>();
	check(Settings);
	return *Settings;
}

FString UServerRegistrySettings::ResolveApiBaseUrl() const
{
	FString Url = ValueOrCommandLine(ApiBaseUrl, TEXT("RegistryUrl="));
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	return Url;
}

FString UServerRegistrySettings::ResolveServerApiKey() const
{
	return ValueOrCommandLine(ServerApiKey, TEXT("RegistryKey="));
}

FString UServerRegistrySettings::ResolveServerName() const
{
	const FString Resolved = ValueOrCommandLine(ServerName, TEXT("ServerName="));
	return Resolved.IsEmpty() ? FPlatformProcess::ComputerName() : Resolved;
}

FString UServerRegistrySettings::ResolveRegion() const
{
	return ValueOrCommandLine(Region, TEXT("ServerRegion="));
}

FString UServerRegistrySettings::ResolvePublicIp() const
{
	return ValueOrCommandLine(PublicIpOverride, TEXT("PublicIp="));
}

FString UServerRegistrySettings::ResolveAutoLoginUsername() const
{
	return ValueOrCommandLine(AutoLoginUsername, TEXT("RegistryUser="));
}

FString UServerRegistrySettings::ResolveAutoLoginPassword() const
{
	return ValueOrCommandLine(AutoLoginPassword, TEXT("RegistryPassword="));
}

FName UServerRegistrySettings::GetCategoryName() const
{
	return FName(TEXT("Game"));
}
