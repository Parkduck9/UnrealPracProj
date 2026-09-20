using System.Net;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Options;
using ServerRegistry;

var builder = WebApplication.CreateBuilder(args);

builder.Services.Configure<RegistryOptions>(builder.Configuration.GetSection(RegistryOptions.SectionName));
builder.Services.AddSingleton<RegistryStore>();
builder.Services.AddHostedService<RegistryJanitor>();

var app = builder.Build();

app.UseDefaultFiles();
app.UseStaticFiles();

var store = app.Services.GetRequiredService<RegistryStore>();
var options = app.Services.GetRequiredService<IOptions<RegistryOptions>>().Value;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static IResult Message(string text, int statusCode = StatusCodes.Status200OK) =>
    Results.Json(new MessageResponse(text), statusCode: statusCode);

/// <summary>Servers authenticate with a shared secret so clients cannot publish fake entries.</summary>
bool HasValidServerKey(HttpContext context) =>
    context.Request.Headers.TryGetValue("X-Server-Key", out var supplied) &&
    string.Equals(supplied.ToString(), options.ServerApiKey, StringComparison.Ordinal);

/// <summary>Reads the bearer token and resolves it to a live session, or null.</summary>
Session? ResolveCaller(HttpContext context)
{
    var header = context.Request.Headers.Authorization.ToString();
    if (string.IsNullOrWhiteSpace(header) || !header.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase))
    {
        return null;
    }

    return store.ResolveSession(header["Bearer ".Length..].Trim());
}

/// <summary>
/// Picks the address clients should dial. A server that knows its public address sends it;
/// otherwise the socket the request came in on is the authoritative answer.
/// </summary>
static string ResolvePublishedIp(HttpContext context, string? supplied)
{
    if (!string.IsNullOrWhiteSpace(supplied) && !string.Equals(supplied.Trim(), "auto", StringComparison.OrdinalIgnoreCase))
    {
        return supplied.Trim();
    }

    var remote = context.Connection.RemoteIpAddress;
    if (remote is null)
    {
        return "127.0.0.1";
    }

    if (remote.IsIPv4MappedToIPv6)
    {
        remote = remote.MapToIPv4();
    }

    // ::1 is useless to an Unreal client; hand back the IPv4 loopback it can actually connect to.
    return IPAddress.IsLoopback(remote) ? "127.0.0.1" : remote.ToString();
}

// ---------------------------------------------------------------------------
// Auth - demo only. Any username/password of a sane shape is accepted, because this
// sample is about server discovery, not identity. Docs/ARCHITECTURE.md says what a
// real deployment has to replace here.
// ---------------------------------------------------------------------------

app.MapPost("/api/auth/login", (LoginRequest request) =>
{
    var username = request.Username?.Trim() ?? string.Empty;
    var password = request.Password ?? string.Empty;

    if (!Regex.IsMatch(username, "^[A-Za-z0-9_.-]{2,24}$"))
    {
        return Message("Username must be 2-24 characters: letters, digits, dot, dash or underscore.",
            StatusCodes.Status400BadRequest);
    }

    if (password.Length < options.MinimumPasswordLength)
    {
        return Message($"Password must be at least {options.MinimumPasswordLength} characters.",
            StatusCodes.Status400BadRequest);
    }

    var session = store.CreateSession(username, out var token);
    return Results.Ok(new LoginResponse(token, username, session.ExpiresAtUtc.UtcDateTime.ToString("o")));
});

app.MapPost("/api/auth/logout", (HttpContext context) =>
{
    var header = context.Request.Headers.Authorization.ToString();
    if (header.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase))
    {
        store.EndSession(header["Bearer ".Length..].Trim());
    }

    return Message("Logged out.");
});

// ---------------------------------------------------------------------------
// Dedicated server endpoints
// ---------------------------------------------------------------------------

app.MapPost("/api/servers/register", (HttpContext context, RegisterRequest request) =>
{
    if (!HasValidServerKey(context))
    {
        return Message("Missing or invalid X-Server-Key.", StatusCodes.Status401Unauthorized);
    }

    if (request.Port is <= 0 or > 65535)
    {
        return Message("Port must be between 1 and 65535.", StatusCodes.Status400BadRequest);
    }

    var entry = store.Register(request, ResolvePublishedIp(context, request.Ip));

    return Results.Ok(new RegisterResponse(
        entry.ServerId,
        entry.Ip,
        entry.Port,
        options.HeartbeatIntervalSeconds,
        $"Registered {entry.ServerName} at {entry.Ip}:{entry.Port}."));
});

app.MapPost("/api/servers/heartbeat", (HttpContext context, HeartbeatRequest request) =>
{
    if (!HasValidServerKey(context))
    {
        return Message("Missing or invalid X-Server-Key.", StatusCodes.Status401Unauthorized);
    }

    // 404 is meaningful here: it tells the server its entry is gone and it should re-register.
    return store.Heartbeat(request)
        ? Message("ok")
        : Message("Unknown serverId. Register again.", StatusCodes.Status404NotFound);
});

app.MapPost("/api/servers/unregister", (HttpContext context, UnregisterRequest request) =>
{
    if (!HasValidServerKey(context))
    {
        return Message("Missing or invalid X-Server-Key.", StatusCodes.Status401Unauthorized);
    }

    // Idempotent: shutting down twice is not an error.
    store.Unregister(request.ServerId);
    return Message("Unregistered.");
});

// ---------------------------------------------------------------------------
// Client endpoints
// ---------------------------------------------------------------------------

app.MapGet("/api/servers", (HttpContext context, string? region, string? version, bool? includeOffline) =>
{
    if (ResolveCaller(context) is null)
    {
        return Message("Log in first: send Authorization: Bearer <token>.", StatusCodes.Status401Unauthorized);
    }

    var servers = store.ListServers(region, version, includeOffline ?? false);
    return Results.Ok(new ServerListResponse(servers, servers.Count));
});

app.MapGet("/api/servers/best", (HttpContext context, string? region, string? version) =>
{
    if (ResolveCaller(context) is null)
    {
        return Message("Log in first: send Authorization: Bearer <token>.", StatusCodes.Status401Unauthorized);
    }

    var best = store.FindBestServer(region, version);
    return best is null
        ? Message("No online server with free slots.", StatusCodes.Status404NotFound)
        : Results.Ok(new ServerListResponse([best], 1));
});

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

app.MapGet("/health", () => Results.Ok(new
{
    status = "ok",
    servers = store.ServerCount,
    sessions = store.SessionCount,
    utc = DateTimeOffset.UtcNow.UtcDateTime.ToString("o"),
}));

app.Logger.LogInformation(
    "Server registry listening. Heartbeat every {Heartbeat}s, entries go offline after {Offline}s and are purged after {Purge}s.",
    options.HeartbeatIntervalSeconds, options.OfflineAfterSeconds, options.PurgeAfterSeconds);

if (string.Equals(options.ServerApiKey, "dev-server-key", StringComparison.Ordinal))
{
    app.Logger.LogWarning("Using the default X-Server-Key. Change Registry:ServerApiKey before exposing this registry.");
}

app.Run();
