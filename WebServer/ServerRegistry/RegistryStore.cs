using System.Collections.Concurrent;
using System.Security.Cryptography;
using Microsoft.Extensions.Options;

namespace ServerRegistry;

/// <summary>
/// In-memory store for registered game servers and logged-in clients.
///
/// State lives in the process, so restarting the registry forgets everything. Servers recover
/// on their own: a heartbeat for an unknown id returns 404 and the Unreal side re-registers.
/// Swap this class for a database if entries need to outlive the process.
/// </summary>
public sealed class RegistryStore(IOptions<RegistryOptions> options, ILogger<RegistryStore> logger)
{
    private readonly ConcurrentDictionary<string, ServerEntry> _servers = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, Session> _sessions = new(StringComparer.Ordinal);
    private readonly RegistryOptions _options = options.Value;

    public RegistryOptions Options => _options;

    public int ServerCount => _servers.Count;

    public int SessionCount => _sessions.Count;

    // -----------------------------------------------------------------------
    // Servers
    // -----------------------------------------------------------------------

    public ServerEntry Register(RegisterRequest request, string resolvedIp)
    {
        var entry = new ServerEntry
        {
            ServerId = Guid.NewGuid().ToString("N"),
            ServerName = Sanitize(request.ServerName, "Unnamed Server", 64),
            Ip = resolvedIp,
            Port = request.Port,
            MapName = Sanitize(request.MapName, string.Empty, 128),
            MaxPlayers = Math.Clamp(request.MaxPlayers, 0, 1000),
            Region = Sanitize(request.Region, string.Empty, 32),
            Version = Sanitize(request.Version, string.Empty, 64),
            LocalIp = Sanitize(request.LocalIp, string.Empty, 64),
        };

        // Re-registering the same endpoint replaces the old entry instead of duplicating it,
        // which is what happens when a server process is restarted without a clean shutdown.
        foreach (var stale in _servers.Values.Where(s => s.Ip == entry.Ip && s.Port == entry.Port).ToList())
        {
            if (_servers.TryRemove(stale.ServerId, out _))
            {
                logger.LogInformation("Replaced stale entry {OldId} for {Ip}:{Port}", stale.ServerId, stale.Ip, stale.Port);
            }
        }

        _servers[entry.ServerId] = entry;
        logger.LogInformation("Registered {Name} as {Id} at {Ip}:{Port}", entry.ServerName, entry.ServerId, entry.Ip, entry.Port);
        return entry;
    }

    /// <summary>Returns false when the id is unknown, which tells the server to register again.</summary>
    public bool Heartbeat(HeartbeatRequest request)
    {
        if (string.IsNullOrWhiteSpace(request.ServerId) || !_servers.TryGetValue(request.ServerId, out var entry))
        {
            return false;
        }

        entry.LastHeartbeatUtc = DateTimeOffset.UtcNow;
        entry.CurrentPlayers = Math.Max(0, request.CurrentPlayers);

        if (!string.IsNullOrWhiteSpace(request.MapName))
        {
            entry.MapName = Sanitize(request.MapName, entry.MapName, 128);
        }

        return true;
    }

    public bool Unregister(string? serverId)
    {
        if (string.IsNullOrWhiteSpace(serverId) || !_servers.TryRemove(serverId, out var entry))
        {
            return false;
        }

        logger.LogInformation("Unregistered {Name} ({Id})", entry.ServerName, entry.ServerId);
        return true;
    }

    public IReadOnlyList<ServerListItem> ListServers(string? region, string? version, bool includeOffline)
    {
        var offlineAfter = _options.OfflineAfterSeconds;

        return _servers.Values
            .Select(entry => entry.ToListItem(offlineAfter))
            .Where(item => includeOffline || item.Online)
            .Where(item => string.IsNullOrWhiteSpace(region) || string.Equals(item.Region, region, StringComparison.OrdinalIgnoreCase))
            .Where(item => string.IsNullOrWhiteSpace(version) || string.Equals(item.Version, version, StringComparison.OrdinalIgnoreCase))
            .OrderBy(item => item.CurrentPlayers)
            .ThenBy(item => item.ServerName, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    /// <summary>Least loaded online server that still has room, or null when there is none.</summary>
    public ServerListItem? FindBestServer(string? region, string? version) =>
        ListServers(region, version, includeOffline: false)
            .Where(item => item.MaxPlayers <= 0 || item.CurrentPlayers < item.MaxPlayers)
            .MinBy(item => item.CurrentPlayers);

    /// <summary>Drops entries that stopped heartbeating long enough ago to be considered gone.</summary>
    public int PurgeExpired()
    {
        var removed = 0;

        foreach (var entry in _servers.Values.Where(e => e.SecondsSinceHeartbeat > _options.PurgeAfterSeconds).ToList())
        {
            if (_servers.TryRemove(entry.ServerId, out _))
            {
                removed++;
                logger.LogInformation("Purged {Name} ({Id}): silent for {Seconds:F0}s",
                    entry.ServerName, entry.ServerId, entry.SecondsSinceHeartbeat);
            }
        }

        foreach (var (token, session) in _sessions.ToList())
        {
            if (session.IsExpired)
            {
                _sessions.TryRemove(token, out _);
            }
        }

        return removed;
    }

    // -----------------------------------------------------------------------
    // Sessions
    // -----------------------------------------------------------------------

    public Session CreateSession(string username, out string token)
    {
        token = Convert.ToHexString(RandomNumberGenerator.GetBytes(32)).ToLowerInvariant();
        var session = new Session(username, DateTimeOffset.UtcNow.AddHours(_options.SessionLifetimeHours));
        _sessions[token] = session;
        logger.LogInformation("Issued a session for {Username}", username);
        return session;
    }

    public Session? ResolveSession(string? token)
    {
        if (string.IsNullOrWhiteSpace(token) || !_sessions.TryGetValue(token, out var session))
        {
            return null;
        }

        if (session.IsExpired)
        {
            _sessions.TryRemove(token, out _);
            return null;
        }

        return session;
    }

    public void EndSession(string? token)
    {
        if (!string.IsNullOrWhiteSpace(token))
        {
            _sessions.TryRemove(token, out _);
        }
    }

    /// <summary>Trims, caps the length and falls back when the value is blank.</summary>
    private static string Sanitize(string? value, string fallback, int maxLength)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return fallback;
        }

        var trimmed = value.Trim();
        return trimmed.Length <= maxLength ? trimmed : trimmed[..maxLength];
    }
}

/// <summary>Sweeps expired servers and sessions so the store does not grow without bound.</summary>
public sealed class RegistryJanitor(RegistryStore store, ILogger<RegistryJanitor> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(30));
        logger.LogInformation("Registry janitor started.");

        while (await timer.WaitForNextTickAsync(stoppingToken))
        {
            try
            {
                store.PurgeExpired();
            }
            catch (Exception ex)
            {
                logger.LogError(ex, "Purge pass failed.");
            }
        }
    }
}
