// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServerRegistrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "IPAddress.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ServerRegistrySettings.h"
#include "SocketSubsystem.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogServerRegistry);

namespace ServerRegistryPaths
{
	static const TCHAR* Login = TEXT("/api/auth/login");
	static const TCHAR* Servers = TEXT("/api/servers");
	static const TCHAR* Register = TEXT("/api/servers/register");
	static const TCHAR* Heartbeat = TEXT("/api/servers/heartbeat");
	static const TCHAR* Unregister = TEXT("/api/servers/unregister");
}

namespace
{
	using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	using FCondensedJsonWriterFactory = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	/** Parses a response body into a JSON object. Returns false on empty or malformed bodies. */
	bool ParseJsonBody(const FHttpResponsePtr& Response, TSharedPtr<FJsonObject>& OutObject)
	{
		if (!Response.IsValid())
		{
			return false;
		}

		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	/** Reads one server entry. The registry publishes camelCase keys; see Docs/API.md. */
	FGameServerInfo ParseServerEntry(const TSharedPtr<FJsonObject>& Object)
	{
		FGameServerInfo Info;
		if (!Object.IsValid())
		{
			return Info;
		}

		Object->TryGetStringField(TEXT("serverId"), Info.ServerId);
		Object->TryGetStringField(TEXT("serverName"), Info.ServerName);
		Object->TryGetStringField(TEXT("ip"), Info.Ip);
		Object->TryGetNumberField(TEXT("port"), Info.Port);
		Object->TryGetStringField(TEXT("mapName"), Info.MapName);
		Object->TryGetNumberField(TEXT("currentPlayers"), Info.CurrentPlayers);
		Object->TryGetNumberField(TEXT("maxPlayers"), Info.MaxPlayers);
		Object->TryGetStringField(TEXT("region"), Info.Region);
		Object->TryGetStringField(TEXT("version"), Info.Version);
		Object->TryGetBoolField(TEXT("online"), Info.bOnline);

		double SecondsSinceHeartbeat = 0.0;
		if (Object->TryGetNumberField(TEXT("secondsSinceHeartbeat"), SecondsSinceHeartbeat))
		{
			Info.SecondsSinceHeartbeat = static_cast<float>(SecondsSinceHeartbeat);
		}

		return Info;
	}

	bool IsSuccessCode(const FHttpResponsePtr& Response)
	{
		return Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
	}
}

// =====================================================================================
// Lifetime
// =====================================================================================

void UServerRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The world does not exist yet, so the real work waits for the first map load.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UServerRegistrySubsystem::HandlePostLoadMap);

	const FString BaseUrl = UServerRegistrySettings::Get().ResolveApiBaseUrl();

	UE_LOG(LogServerRegistry, Log, TEXT("Server registry subsystem ready. Registry=%s Role=%s"),
		*BaseUrl, IsRunningDedicatedServer() ? TEXT("DedicatedServer") : TEXT("Client"));

	// Unreal config files treat "//" as a comment, so an unquoted URL silently becomes "http:"
	// and every request then fails with an unhelpful DNS error. Catch it here and say what to fix.
	if (!BaseUrl.StartsWith(TEXT("http://")) && !BaseUrl.StartsWith(TEXT("https://")))
	{
		UE_LOG(LogServerRegistry, Error,
			TEXT("ApiBaseUrl resolved to '%s', which is not a usable URL. Quote the value in ")
			TEXT("DefaultGame.ini so the config parser keeps the slashes: ApiBaseUrl=\"http://host:port\""),
			*BaseUrl);
	}
}

void UServerRegistrySubsystem::Deinitialize()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}

	// Best effort: take the entry out of the registry on a clean shutdown. If the process is
	// killed instead, the registry expires the entry once heartbeats stop.
	if (IsServerRegistered())
	{
		UnregisterThisServer();
		FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::Shutdown);
	}

	StopHeartbeat();

	Super::Deinitialize();
}

void UServerRegistrySubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	const UServerRegistrySettings& Settings = UServerRegistrySettings::Get();

	if (IsRunningDedicatedServer())
	{
		if (Settings.bAutoRegisterOnDedicatedServer && !IsServerRegistered())
		{
			RegisterThisServer();
		}
		return;
	}

	// Client: optional headless auto-login so the flow can be exercised without a login widget.
	if (bAutoLoginAttempted || !Settings.bAutoLoginOnClientStart)
	{
		return;
	}

	const FString Username = Settings.ResolveAutoLoginUsername();
	const FString Password = Settings.ResolveAutoLoginPassword();
	if (Username.IsEmpty())
	{
		return;
	}

	bAutoLoginAttempted = true;
	UE_LOG(LogServerRegistry, Log, TEXT("Auto-login as '%s' (AutoJoin=%s)"),
		*Username, Settings.bAutoJoinAfterLogin ? TEXT("true") : TEXT("false"));

	if (Settings.bAutoJoinAfterLogin)
	{
		LoginAndJoinBestServer(Username, Password);
	}
	else
	{
		Login(Username, Password);
	}
}

// =====================================================================================
// HTTP plumbing
// =====================================================================================

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UServerRegistrySubsystem::MakeRequest(
	const FString& Verb, const FString& RelativePath, bool bAttachServerKey, bool bAttachAuthToken) const
{
	const UServerRegistrySettings& Settings = UServerRegistrySettings::Get();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings.ResolveApiBaseUrl() + RelativePath);
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetTimeout(Settings.RequestTimeoutSeconds);

	if (bAttachServerKey)
	{
		Request->SetHeader(TEXT("X-Server-Key"), Settings.ResolveServerApiKey());
	}

	if (bAttachAuthToken && !LoginResult.Token.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + LoginResult.Token);
	}

	return Request;
}

FString UServerRegistrySubsystem::ExtractMessage(const FHttpResponsePtr& Response, const FString& DefaultMessage)
{
	TSharedPtr<FJsonObject> Json;
	if (ParseJsonBody(Response, Json))
	{
		FString Message;
		if (Json->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty())
		{
			return Message;
		}
	}

	if (Response.IsValid())
	{
		return FString::Printf(TEXT("%s (HTTP %d)"), *DefaultMessage, Response->GetResponseCode());
	}

	return DefaultMessage;
}

// =====================================================================================
// Dedicated server: register / heartbeat / unregister
// =====================================================================================

void UServerRegistrySubsystem::RegisterThisServer()
{
	if (bRegisterInFlight)
	{
		UE_LOG(LogServerRegistry, Verbose, TEXT("Register skipped: a request is already in flight."));
		return;
	}

	if (IsServerRegistered())
	{
		UE_LOG(LogServerRegistry, Verbose, TEXT("Register skipped: already registered as %s."), *RegisteredServerId);
		return;
	}

	const UServerRegistrySettings& Settings = UServerRegistrySettings::Get();

	FString Body;
	const TSharedRef<FCondensedJsonWriter> Writer = FCondensedJsonWriterFactory::Create(&Body);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("serverName"), Settings.ResolveServerName());
	// An empty ip tells the registry to record the address the request arrived from, which is
	// the correct public address when the server sits behind NAT.
	Writer->WriteValue(TEXT("ip"), Settings.ResolvePublicIp());
	Writer->WriteValue(TEXT("port"), ResolveGamePort());
	Writer->WriteValue(TEXT("mapName"), ResolveCurrentMapName());
	Writer->WriteValue(TEXT("maxPlayers"), Settings.MaxPlayers);
	Writer->WriteValue(TEXT("region"), Settings.ResolveRegion());
	Writer->WriteValue(TEXT("version"), FApp::GetBuildVersion());
	Writer->WriteValue(TEXT("localIp"), ResolveLocalIpAddress());
	Writer->WriteObjectEnd();
	Writer->Close();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(TEXT("POST"), ServerRegistryPaths::Register, true, false);
	Request->SetContentAsString(Body);
	Request->OnProcessRequestComplete().BindUObject(this, &UServerRegistrySubsystem::HandleRegisterResponse);

	bRegisterInFlight = true;
	UE_LOG(LogServerRegistry, Log, TEXT("Registering server: %s"), *Body);

	if (!Request->ProcessRequest())
	{
		bRegisterInFlight = false;
		const FString Message = TEXT("Failed to dispatch the register request.");
		UE_LOG(LogServerRegistry, Error, TEXT("%s"), *Message);
		OnRegistrationChanged.Broadcast(false, Message);
	}
}

void UServerRegistrySubsystem::HandleRegisterResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	bRegisterInFlight = false;

	if (!bConnectedSuccessfully || !IsSuccessCode(Response))
	{
		const FString Message = bConnectedSuccessfully
			? ExtractMessage(Response, TEXT("Registry rejected the registration"))
			: TEXT("Could not reach the registry. Is the web server running?");

		UE_LOG(LogServerRegistry, Error, TEXT("Register failed: %s"), *Message);
		OnRegistrationChanged.Broadcast(false, Message);
		return;
	}

	TSharedPtr<FJsonObject> Json;
	if (!ParseJsonBody(Response, Json) || !Json->TryGetStringField(TEXT("serverId"), RegisteredServerId) || RegisteredServerId.IsEmpty())
	{
		const FString Message = TEXT("Registry response did not contain a serverId.");
		UE_LOG(LogServerRegistry, Error, TEXT("%s"), *Message);
		OnRegistrationChanged.Broadcast(false, Message);
		return;
	}

	FString PublishedIp;
	Json->TryGetStringField(TEXT("ip"), PublishedIp);

	UE_LOG(LogServerRegistry, Log, TEXT("Registered as %s. Clients will be sent to %s:%d"),
		*RegisteredServerId, *PublishedIp, ResolveGamePort());

	StartHeartbeat();
	OnRegistrationChanged.Broadcast(true, FString::Printf(TEXT("Registered as %s (%s)"), *RegisteredServerId, *PublishedIp));
}

void UServerRegistrySubsystem::UnregisterThisServer()
{
	if (!IsServerRegistered())
	{
		return;
	}

	FString Body;
	const TSharedRef<FCondensedJsonWriter> Writer = FCondensedJsonWriterFactory::Create(&Body);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("serverId"), RegisteredServerId);
	Writer->WriteObjectEnd();
	Writer->Close();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(TEXT("POST"), ServerRegistryPaths::Unregister, true, false);
	Request->SetContentAsString(Body);
	Request->ProcessRequest();

	UE_LOG(LogServerRegistry, Log, TEXT("Unregistering server %s."), *RegisteredServerId);

	StopHeartbeat();
	RegisteredServerId.Reset();
	OnRegistrationChanged.Broadcast(false, TEXT("Unregistered."));
}

void UServerRegistrySubsystem::SetPublishedPlayerCount(int32 InPlayerCount)
{
	OverridePlayerCount = FMath::Max(0, InPlayerCount);
}

void UServerRegistrySubsystem::StartHeartbeat()
{
	StopHeartbeat();

	const float Interval = FMath::Max(1.0f, UServerRegistrySettings::Get().HeartbeatIntervalSeconds);
	HeartbeatTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UServerRegistrySubsystem::TickHeartbeat), Interval);

	UE_LOG(LogServerRegistry, Log, TEXT("Heartbeat started at %.1fs."), Interval);
}

void UServerRegistrySubsystem::StopHeartbeat()
{
	if (HeartbeatTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatTickerHandle);
		HeartbeatTickerHandle.Reset();
	}
}

bool UServerRegistrySubsystem::TickHeartbeat(float DeltaTime)
{
	if (!IsServerRegistered())
	{
		return false; // Stop ticking once the entry is gone.
	}

	SendHeartbeat();
	return true;
}

void UServerRegistrySubsystem::SendHeartbeat()
{
	FString Body;
	const TSharedRef<FCondensedJsonWriter> Writer = FCondensedJsonWriterFactory::Create(&Body);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("serverId"), RegisteredServerId);
	Writer->WriteValue(TEXT("currentPlayers"), ResolveCurrentPlayerCount());
	Writer->WriteValue(TEXT("mapName"), ResolveCurrentMapName());
	Writer->WriteObjectEnd();
	Writer->Close();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(TEXT("POST"), ServerRegistryPaths::Heartbeat, true, false);
	Request->SetContentAsString(Body);

	// A heartbeat that 404s means the registry expired or restarted: re-register instead of
	// quietly disappearing from the server browser.
	Request->OnProcessRequestComplete().BindUObject(this, &UServerRegistrySubsystem::HandleHeartbeatResponse);
	Request->ProcessRequest();
}

void UServerRegistrySubsystem::HandleHeartbeatResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully)
	{
		UE_LOG(LogServerRegistry, Warning, TEXT("Heartbeat could not reach the registry; will retry."));
		return;
	}

	if (IsSuccessCode(Response))
	{
		UE_LOG(LogServerRegistry, Verbose, TEXT("Heartbeat ok (%d players)."), ResolveCurrentPlayerCount());
		return;
	}

	if (Response.IsValid() && Response->GetResponseCode() == EHttpResponseCodes::NotFound)
	{
		UE_LOG(LogServerRegistry, Warning, TEXT("Registry no longer knows server %s; re-registering."), *RegisteredServerId);
		StopHeartbeat();
		RegisteredServerId.Reset();
		RegisterThisServer();
		return;
	}

	UE_LOG(LogServerRegistry, Warning, TEXT("Heartbeat rejected: %s"), *ExtractMessage(Response, TEXT("unknown error")));
}

// =====================================================================================
// Client: login / list / join
// =====================================================================================

void UServerRegistrySubsystem::Login(const FString& Username, const FString& Password)
{
	FString Body;
	const TSharedRef<FCondensedJsonWriter> Writer = FCondensedJsonWriterFactory::Create(&Body);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("username"), Username);
	Writer->WriteValue(TEXT("password"), Password);
	Writer->WriteObjectEnd();
	Writer->Close();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(TEXT("POST"), ServerRegistryPaths::Login, false, false);
	Request->SetContentAsString(Body);
	Request->OnProcessRequestComplete().BindUObject(this, &UServerRegistrySubsystem::HandleLoginResponse, false);

	UE_LOG(LogServerRegistry, Log, TEXT("Logging in as '%s'."), *Username);

	if (!Request->ProcessRequest())
	{
		OnLoginCompleted.Broadcast(false, TEXT("Failed to dispatch the login request."));
	}
}

void UServerRegistrySubsystem::LoginAndJoinBestServer(const FString& Username, const FString& Password)
{
	FString Body;
	const TSharedRef<FCondensedJsonWriter> Writer = FCondensedJsonWriterFactory::Create(&Body);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("username"), Username);
	Writer->WriteValue(TEXT("password"), Password);
	Writer->WriteObjectEnd();
	Writer->Close();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(TEXT("POST"), ServerRegistryPaths::Login, false, false);
	Request->SetContentAsString(Body);
	// The trailing "true" makes the completion handler chain straight into the server list.
	Request->OnProcessRequestComplete().BindUObject(this, &UServerRegistrySubsystem::HandleLoginResponse, true);

	UE_LOG(LogServerRegistry, Log, TEXT("Logging in as '%s' and joining the best server."), *Username);

	if (!Request->ProcessRequest())
	{
		OnLoginCompleted.Broadcast(false, TEXT("Failed to dispatch the login request."));
	}
}

void UServerRegistrySubsystem::HandleLoginResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, bool bJoinBestWhenDone)
{
	if (!bConnectedSuccessfully || !IsSuccessCode(Response))
	{
		const FString Message = bConnectedSuccessfully
			? ExtractMessage(Response, TEXT("Login rejected"))
			: TEXT("Could not reach the registry. Is the web server running?");

		UE_LOG(LogServerRegistry, Error, TEXT("Login failed: %s"), *Message);
		OnLoginCompleted.Broadcast(false, Message);
		return;
	}

	TSharedPtr<FJsonObject> Json;
	FRegistryLoginResult Parsed;
	if (!ParseJsonBody(Response, Json) || !Json->TryGetStringField(TEXT("token"), Parsed.Token) || Parsed.Token.IsEmpty())
	{
		const FString Message = TEXT("Login response did not contain a token.");
		UE_LOG(LogServerRegistry, Error, TEXT("%s"), *Message);
		OnLoginCompleted.Broadcast(false, Message);
		return;
	}

	Json->TryGetStringField(TEXT("username"), Parsed.Username);
	Json->TryGetStringField(TEXT("expiresAtUtc"), Parsed.ExpiresAtUtc);
	LoginResult = Parsed;

	UE_LOG(LogServerRegistry, Log, TEXT("Logged in as '%s'."), *LoginResult.Username);
	OnLoginCompleted.Broadcast(true, FString::Printf(TEXT("Logged in as %s"), *LoginResult.Username));

	if (bJoinBestWhenDone)
	{
		// Same request as RequestServerList, but the handler travels once the list arrives.
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> ListRequest = MakeRequest(TEXT("GET"), ServerRegistryPaths::Servers, false, true);
		ListRequest->OnProcessRequestComplete().BindUObject(this, &UServerRegistrySubsystem::HandleServerListResponse, true);
		ListRequest->ProcessRequest();
	}
}

void UServerRegistrySubsystem::Logout()
{
	LoginResult = FRegistryLoginResult();
	CachedServers.Reset();
	UE_LOG(LogServerRegistry, Log, TEXT("Logged out."));
}

void UServerRegistrySubsystem::RequestServerList()
{
	if (!IsLoggedIn())
	{
		const FString Message = TEXT("Log in before requesting the server list.");
		UE_LOG(LogServerRegistry, Warning, TEXT("%s"), *Message);
		OnServerListReceived.Broadcast(false, TArray<FGameServerInfo>(), Message);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(TEXT("GET"), ServerRegistryPaths::Servers, false, true);
	Request->OnProcessRequestComplete().BindUObject(this, &UServerRegistrySubsystem::HandleServerListResponse, false);

	if (!Request->ProcessRequest())
	{
		OnServerListReceived.Broadcast(false, TArray<FGameServerInfo>(), TEXT("Failed to dispatch the server list request."));
	}
}

void UServerRegistrySubsystem::HandleServerListResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, bool bJoinBestWhenDone)
{
	if (!bConnectedSuccessfully || !IsSuccessCode(Response))
	{
		const FString Message = bConnectedSuccessfully
			? ExtractMessage(Response, TEXT("Server list request rejected"))
			: TEXT("Could not reach the registry. Is the web server running?");

		UE_LOG(LogServerRegistry, Error, TEXT("Server list failed: %s"), *Message);
		OnServerListReceived.Broadcast(false, TArray<FGameServerInfo>(), Message);
		return;
	}

	TSharedPtr<FJsonObject> Json;
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!ParseJsonBody(Response, Json) || !Json->TryGetArrayField(TEXT("servers"), Entries))
	{
		const FString Message = TEXT("Server list response was not in the expected shape.");
		UE_LOG(LogServerRegistry, Error, TEXT("%s"), *Message);
		OnServerListReceived.Broadcast(false, TArray<FGameServerInfo>(), Message);
		return;
	}

	CachedServers.Reset();
	for (const TSharedPtr<FJsonValue>& Entry : *Entries)
	{
		const FGameServerInfo Info = ParseServerEntry(Entry.IsValid() ? Entry->AsObject() : nullptr);
		if (Info.IsValidEntry())
		{
			CachedServers.Add(Info);
		}
	}

	UE_LOG(LogServerRegistry, Log, TEXT("Received %d server(s) from the registry."), CachedServers.Num());
	OnServerListReceived.Broadcast(true, CachedServers, FString::Printf(TEXT("%d server(s)"), CachedServers.Num()));

	if (bJoinBestWhenDone)
	{
		JoinBestServer();
	}
}

FGameServerInfo UServerRegistrySubsystem::GetBestCachedServer(bool& bFound) const
{
	FGameServerInfo Best;
	bFound = false;

	for (const FGameServerInfo& Candidate : CachedServers)
	{
		if (!Candidate.bOnline || !Candidate.IsValidEntry() || Candidate.IsFull())
		{
			continue;
		}

		if (!bFound || Candidate.CurrentPlayers < Best.CurrentPlayers)
		{
			Best = Candidate;
			bFound = true;
		}
	}

	return Best;
}

bool UServerRegistrySubsystem::JoinBestServer()
{
	bool bFound = false;
	const FGameServerInfo Best = GetBestCachedServer(bFound);

	if (!bFound)
	{
		const FString Message = TEXT("No online server with room is registered.");
		UE_LOG(LogServerRegistry, Warning, TEXT("%s"), *Message);
		OnJoinServer.Broadcast(false, Message);
		return false;
	}

	return JoinServer(Best);
}

bool UServerRegistrySubsystem::JoinServerById(const FString& ServerId)
{
	const FGameServerInfo* Match = CachedServers.FindByPredicate(
		[&ServerId](const FGameServerInfo& Candidate) { return Candidate.ServerId == ServerId; });

	if (!Match)
	{
		const FString Message = FString::Printf(TEXT("Server %s is not in the cached list."), *ServerId);
		UE_LOG(LogServerRegistry, Warning, TEXT("%s"), *Message);
		OnJoinServer.Broadcast(false, Message);
		return false;
	}

	return JoinServer(*Match);
}

bool UServerRegistrySubsystem::JoinServer(const FGameServerInfo& Server)
{
	if (!Server.IsValidEntry())
	{
		const FString Message = TEXT("Server entry has no usable address.");
		UE_LOG(LogServerRegistry, Warning, TEXT("%s"), *Message);
		OnJoinServer.Broadcast(false, Message);
		return false;
	}

	const FString ConnectString = Server.ToConnectString();
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;

	if (!World)
	{
		const FString Message = TEXT("No world is loaded, so travelling is not possible yet.");
		UE_LOG(LogServerRegistry, Error, TEXT("%s"), *Message);
		OnJoinServer.Broadcast(false, Message);
		return false;
	}

	UE_LOG(LogServerRegistry, Log, TEXT("Travelling to %s (%s)."), *ConnectString, *Server.ServerName);
	OnJoinServer.Broadcast(true, FString::Printf(TEXT("Connecting to %s"), *ConnectString));

	if (APlayerController* PlayerController = GetGameInstance()->GetFirstLocalPlayerController(World))
	{
		PlayerController->ClientTravel(ConnectString, ETravelType::TRAVEL_Absolute);
	}
	else
	{
		// No controller yet (for example on a menu map without a pawn): fall back to engine travel.
		GEngine->SetClientTravel(World, *ConnectString, ETravelType::TRAVEL_Absolute);
	}

	return true;
}

// =====================================================================================
// Local environment probes
// =====================================================================================

FString UServerRegistrySubsystem::ResolveLocalIpAddress()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		return FString();
	}

	bool bCanBindAll = false;
	const TSharedPtr<FInternetAddr> Addr = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	return (Addr.IsValid() && Addr->IsValid()) ? Addr->ToString(false) : FString();
}

int32 UServerRegistrySubsystem::ResolveGamePort() const
{
	int32 Port = 7777;

	if (const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (World->URL.Port > 0)
		{
			Port = World->URL.Port;
		}
	}

	// -port= on the command line is what actually binds the listen socket, so it wins.
	int32 CommandLinePort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("port="), CommandLinePort) && CommandLinePort > 0)
	{
		Port = CommandLinePort;
	}

	return Port;
}

int32 UServerRegistrySubsystem::ResolveCurrentPlayerCount() const
{
	if (OverridePlayerCount >= 0)
	{
		return OverridePlayerCount;
	}

	if (const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->PlayerArray.Num();
		}
	}

	return 0;
}

FString UServerRegistrySubsystem::ResolveCurrentMapName() const
{
	if (const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		// Strips the PIE prefix so editor and packaged runs report the same name.
		return UWorld::RemovePIEPrefix(World->GetMapName());
	}

	return FString();
}
