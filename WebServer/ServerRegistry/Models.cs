namespace ServerRegistry;

// ---------------------------------------------------------------------------
// Requests sent by the Unreal dedicated server (authenticated with X-Server-Key)
// ---------------------------------------------------------------------------

/// <param name="Ip">
/// Leave null or empty to have the registry record the address the request arrived from.
/// That is the right choice behind NAT, where the server cannot see its own public address.
/// </param>
public sealed record RegisterRequest(
    string? ServerName,
    string? Ip,
    int Port,
    string? MapName,
    int MaxPlayers,
    string? Region,
    string? Version,
    string? LocalIp);

public sealed record HeartbeatRequest(
    string? ServerId,
    int CurrentPlayers,
    string? MapName);

public sealed record UnregisterRequest(string? ServerId);

// ---------------------------------------------------------------------------
// Requests sent by the Unreal client
// ---------------------------------------------------------------------------

public sealed record LoginRequest(string? Username, string? Password);

// ---------------------------------------------------------------------------
// Responses
// ---------------------------------------------------------------------------

public sealed record RegisterResponse(
    string ServerId,
    string Ip,
    int Port,
    double HeartbeatIntervalSeconds,
    string Message);

public sealed record LoginResponse(
    string Token,
    string Username,
    string ExpiresAtUtc);

/// <summary>Shape consumed by FGameServerInfo on the Unreal side.</summary>
public sealed record ServerListItem(
    string ServerId,
    string ServerName,
    string Ip,
    int Port,
    string MapName,
    int CurrentPlayers,
    int MaxPlayers,
    string Region,
    string Version,
    bool Online,
    double SecondsSinceHeartbeat);

public sealed record ServerListResponse(IReadOnlyList<ServerListItem> Servers, int Count);

public sealed record MessageResponse(string Message);

// ---------------------------------------------------------------------------
// Stored state
// ---------------------------------------------------------------------------

/// <summary>A live registry entry. Mutated in place by heartbeats.</summary>
public sealed class ServerEntry
{
    public required string ServerId { get; init; }
    public required string ServerName { get; set; }
    public required string Ip { get; set; }
    public required int Port { get; set; }
    public string MapName { get; set; } = string.Empty;
    public int CurrentPlayers { get; set; }
    public int MaxPlayers { get; set; }
    public string Region { get; set; } = string.Empty;
    public string Version { get; set; } = string.Empty;

    /// <summary>LAN address reported by the server, kept for diagnostics only.</summary>
    public string LocalIp { get; set; } = string.Empty;

    public DateTimeOffset RegisteredAtUtc { get; init; } = DateTimeOffset.UtcNow;
    public DateTimeOffset LastHeartbeatUtc { get; set; } = DateTimeOffset.UtcNow;

    public double SecondsSinceHeartbeat => (DateTimeOffset.UtcNow - LastHeartbeatUtc).TotalSeconds;

    public ServerListItem ToListItem(double offlineAfterSeconds) => new(
        ServerId,
        ServerName,
        Ip,
        Port,
        MapName,
        CurrentPlayers,
        MaxPlayers,
        Region,
        Version,
        SecondsSinceHeartbeat <= offlineAfterSeconds,
        Math.Round(SecondsSinceHeartbeat, 1));
}

/// <summary>A logged-in client. Demo only - see Docs/ARCHITECTURE.md.</summary>
public sealed record Session(string Username, DateTimeOffset ExpiresAtUtc)
{
    public bool IsExpired => DateTimeOffset.UtcNow >= ExpiresAtUtc;
}

/// <summary>Bound from the "Registry" section of appsettings.json.</summary>
public sealed class RegistryOptions
{
    public const string SectionName = "Registry";

    /// <summary>Shared secret the Unreal server sends as X-Server-Key.</summary>
    public string ServerApiKey { get; set; } = "dev-server-key";

    /// <summary>How often servers are told to heartbeat.</summary>
    public double HeartbeatIntervalSeconds { get; set; } = 10;

    /// <summary>Entries go offline after this many missed heartbeats.</summary>
    public int OfflineAfterMissedHeartbeats { get; set; } = 3;

    /// <summary>Offline entries are dropped entirely after this long without a heartbeat.</summary>
    public double PurgeAfterSeconds { get; set; } = 300;

    public double SessionLifetimeHours { get; set; } = 12;

    /// <summary>Minimum password length accepted by the demo login.</summary>
    public int MinimumPasswordLength { get; set; } = 4;

    public double OfflineAfterSeconds => HeartbeatIntervalSeconds * Math.Max(1, OfflineAfterMissedHeartbeats);
}
