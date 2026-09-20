// Copyright Epic Games, Inc. All Rights Reserved.
// Shared data types for the Server Registry (web backend <-> Unreal) feature.

#pragma once

#include "CoreMinimal.h"
#include "ServerRegistryTypes.generated.h"

/**
 * One game server entry as published by the web registry.
 * Mirrors the JSON shape returned by GET /api/servers.
 */
USTRUCT(BlueprintType)
struct FGameServerInfo
{
	GENERATED_BODY()

	/** Registry-assigned unique id (GUID string). */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString ServerId;

	/** Human readable name shown in the server browser. */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString ServerName;

	/** IP address clients should connect to. */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString Ip;

	/** Game port clients should connect to (Unreal default is 7777). */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	int32 Port = 7777;

	/** Map currently loaded on that server. */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString MapName;

	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	int32 MaxPlayers = 0;

	/** Free-form region tag, e.g. "KR", "NA". */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString Region;

	/** Build/version string, used to keep clients off mismatched servers. */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString Version;

	/** False once the registry stops receiving heartbeats from the server. */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	bool bOnline = true;

	/** Seconds since the registry last heard from this server. */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	float SecondsSinceHeartbeat = 0.0f;

	bool IsValidEntry() const
	{
		return !Ip.IsEmpty() && Port > 0;
	}

	bool IsFull() const
	{
		return MaxPlayers > 0 && CurrentPlayers >= MaxPlayers;
	}

	/** "127.0.0.1:7777" - the string handed to ClientTravel / the "open" command. */
	FString ToConnectString() const
	{
		return FString::Printf(TEXT("%s:%d"), *Ip, Port);
	}
};

/** Logged-in player identity returned by POST /api/auth/login. */
USTRUCT(BlueprintType)
struct FRegistryLoginResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString Username;

	/** Bearer token sent on every authenticated client request. */
	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString Token;

	UPROPERTY(BlueprintReadOnly, Category = "Server Registry")
	FString ExpiresAtUtc;
};
