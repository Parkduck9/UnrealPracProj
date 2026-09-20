// Copyright Epic Games, Inc. All Rights Reserved.
// Talks to the web registry: dedicated servers publish their address, clients discover it.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "ServerRegistryTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ServerRegistrySubsystem.generated.h"

PYJ_P38_API DECLARE_LOG_CATEGORY_EXTERN(LogServerRegistry, Log, All);

/** Fired on the client when a login attempt finishes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRegistryLoginCompleted, bool, bSuccess, const FString&, Message);

/** Fired on the client when a server list request finishes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRegistryServerListReceived, bool, bSuccess, const TArray<FGameServerInfo>&, Servers, const FString&, Message);

/** Fired on the dedicated server when it registers, fails to register, or unregisters. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRegistryRegistrationChanged, bool, bRegistered, const FString&, Message);

/** Fired on the client right before travelling, or when travelling could not start. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRegistryJoinServer, bool, bSuccess, const FString&, Message);

/**
 * One subsystem, two roles.
 *
 * Dedicated server: on map load it POSTs its address to the registry, then heartbeats on a timer
 * so the entry stays alive, and unregisters on shutdown.
 *
 * Client: Login() gets a bearer token, RequestServerList() pulls the live entries, and
 * JoinServer() hands the resulting "ip:port" to ClientTravel.
 *
 * Everything is exposed to Blueprint so a login widget can drive it without extra C++.
 */
UCLASS()
class PYJ_P38_API UServerRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	// ---------------------------------------------------------------------
	// Dedicated server side
	// ---------------------------------------------------------------------

	/** POST /api/servers/register. Safe to call twice; a second call is ignored while registered. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Server")
	void RegisterThisServer();

	/** POST /api/servers/unregister and stop heartbeating. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Server")
	void UnregisterThisServer();

	/** Overrides the auto-detected player count published on the next heartbeat. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Server")
	void SetPublishedPlayerCount(int32 InPlayerCount);

	UFUNCTION(BlueprintPure, Category = "Server Registry|Server")
	bool IsServerRegistered() const { return !RegisteredServerId.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Server Registry|Server")
	FString GetRegisteredServerId() const { return RegisteredServerId; }

	// ---------------------------------------------------------------------
	// Client side
	// ---------------------------------------------------------------------

	/** POST /api/auth/login. Result arrives on OnLoginCompleted. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Client")
	void Login(const FString& Username, const FString& Password);

	/** Drops the cached token and server list locally. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Client")
	void Logout();

	/** GET /api/servers. Result arrives on OnServerListReceived. Requires a login. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Client")
	void RequestServerList();

	/** Login, then pull the list, then travel to the least loaded server - the one-call flow. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Client")
	void LoginAndJoinBestServer(const FString& Username, const FString& Password);

	/** ClientTravel to the given entry. Returns false when the entry or the local world is unusable. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Client")
	bool JoinServer(const FGameServerInfo& Server);

	/** ClientTravel to a cached entry by id. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Client")
	bool JoinServerById(const FString& ServerId);

	/** ClientTravel to the online, non-full cached entry with the fewest players. */
	UFUNCTION(BlueprintCallable, Category = "Server Registry|Client")
	bool JoinBestServer();

	UFUNCTION(BlueprintPure, Category = "Server Registry|Client")
	bool IsLoggedIn() const { return !LoginResult.Token.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Server Registry|Client")
	FRegistryLoginResult GetLoginResult() const { return LoginResult; }

	/** Entries from the last successful RequestServerList(). */
	UFUNCTION(BlueprintPure, Category = "Server Registry|Client")
	TArray<FGameServerInfo> GetCachedServers() const { return CachedServers; }

	/** The online, non-full cached entry with the fewest players. bFound is false when there is none. */
	UFUNCTION(BlueprintPure, Category = "Server Registry|Client")
	FGameServerInfo GetBestCachedServer(bool& bFound) const;

	// ---------------------------------------------------------------------
	// Events
	// ---------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Server Registry|Client")
	FOnRegistryLoginCompleted OnLoginCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Server Registry|Client")
	FOnRegistryServerListReceived OnServerListReceived;

	UPROPERTY(BlueprintAssignable, Category = "Server Registry|Client")
	FOnRegistryJoinServer OnJoinServer;

	UPROPERTY(BlueprintAssignable, Category = "Server Registry|Server")
	FOnRegistryRegistrationChanged OnRegistrationChanged;

private:
	/** Builds an absolute URL, sets the JSON headers and applies the configured timeout. */
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FString& Verb, const FString& RelativePath, bool bAttachServerKey, bool bAttachAuthToken) const;

	// Response handlers
	void HandleRegisterResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void HandleHeartbeatResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void HandleLoginResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, bool bJoinBestWhenDone);
	void HandleServerListResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, bool bJoinBestWhenDone);

	/** Called by FTSTicker on the heartbeat interval. Returns true to keep ticking. */
	bool TickHeartbeat(float DeltaTime);
	void SendHeartbeat();
	void StartHeartbeat();
	void StopHeartbeat();

	/** Dedicated servers register here, clients optionally auto-login here. */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** Best-effort primary local address, used when no public IP is configured. */
	static FString ResolveLocalIpAddress();

	/** Listen port from FURL, overridden by -port= on the command line. */
	int32 ResolveGamePort() const;

	/** Live player count from the game state, or the value set via SetPublishedPlayerCount. */
	int32 ResolveCurrentPlayerCount() const;

	/** Current map name, for display in the server browser. */
	FString ResolveCurrentMapName() const;

	/** Pulls "message" out of a JSON body so failures surface a real reason. */
	static FString ExtractMessage(const FHttpResponsePtr& Response, const FString& DefaultMessage);

	/** Registry id for this process. Empty means not registered. */
	FString RegisteredServerId;

	/** Set while a register request is in flight, so retries do not stack up. */
	bool bRegisterInFlight = false;

	/** Negative means derive the count from the game state. */
	int32 OverridePlayerCount = -1;

	/** Keeps the optional client auto-login to a single attempt per process. */
	bool bAutoLoginAttempted = false;

	FRegistryLoginResult LoginResult;
	TArray<FGameServerInfo> CachedServers;

	FTSTicker::FDelegateHandle HeartbeatTickerHandle;
	FDelegateHandle PostLoadMapHandle;
};
