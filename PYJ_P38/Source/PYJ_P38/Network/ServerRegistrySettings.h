// Copyright Epic Games, Inc. All Rights Reserved.
// Project settings for the Server Registry feature (Project Settings -> Game -> Server Registry).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ServerRegistrySettings.generated.h"

/**
 * Configured in Config/DefaultGame.ini under [/Script/PYJ_P38.ServerRegistrySettings].
 *
 * Every value can be overridden on the command line so that one packaged build can
 * be pointed at a different registry without recooking, e.g.
 *   PYJ_P38Server.exe -RegistryUrl=http://10.0.0.5:8080 -ServerName="Seoul #1"
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Server Registry"))
class PYJ_P38_API UServerRegistrySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UServerRegistrySettings();

	/** Base URL of the web registry, without a trailing slash. Override: -RegistryUrl= */
	UPROPERTY(config, EditAnywhere, Category = "Web API")
	FString ApiBaseUrl;

	/** Shared secret sent as X-Server-Key so random clients cannot register servers. Override: -RegistryKey= */
	UPROPERTY(config, EditAnywhere, Category = "Web API")
	FString ServerApiKey;

	/** Seconds before an in-flight HTTP request is abandoned. */
	UPROPERTY(config, EditAnywhere, Category = "Web API", meta = (ClampMin = "1.0"))
	float RequestTimeoutSeconds;

	/** Register with the registry automatically when the process runs as a dedicated server. */
	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server")
	bool bAutoRegisterOnDedicatedServer;

	/** Display name published to clients. Empty falls back to the machine name. Override: -ServerName= */
	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server")
	FString ServerName;

	/** Free-form region tag published to clients. Override: -ServerRegion= */
	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server")
	FString Region;

	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server", meta = (ClampMin = "1"))
	int32 MaxPlayers;

	/** How often the server refreshes its registry entry. The registry expires entries at 3x this value. */
	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server", meta = (ClampMin = "1.0"))
	float HeartbeatIntervalSeconds;

	/**
	 * Address published to clients. Leave empty and the registry records the public address the
	 * request arrived from, which is what you want behind NAT. Override: -PublicIp=
	 */
	UPROPERTY(config, EditAnywhere, Category = "Dedicated Server")
	FString PublicIpOverride;

	/** Log in and travel to a server as soon as the client finishes loading its first map. */
	UPROPERTY(config, EditAnywhere, Category = "Client")
	bool bAutoLoginOnClientStart;

	/** Account used by bAutoLoginOnClientStart. Override: -RegistryUser= */
	UPROPERTY(config, EditAnywhere, Category = "Client")
	FString AutoLoginUsername;

	/** Demo credential only - see Docs/ARCHITECTURE.md for why this must not ship. Override: -RegistryPassword= */
	UPROPERTY(config, EditAnywhere, Category = "Client")
	FString AutoLoginPassword;

	/** After an auto-login, immediately travel to the least loaded server. */
	UPROPERTY(config, EditAnywhere, Category = "Client")
	bool bAutoJoinAfterLogin;

	static const UServerRegistrySettings& Get();

	/** Settings value, then -RegistryUrl= on the command line. Trailing slashes are trimmed. */
	FString ResolveApiBaseUrl() const;
	FString ResolveServerApiKey() const;
	FString ResolveServerName() const;
	FString ResolveRegion() const;
	FString ResolvePublicIp() const;
	FString ResolveAutoLoginUsername() const;
	FString ResolveAutoLoginPassword() const;

	virtual FName GetCategoryName() const override;
};
