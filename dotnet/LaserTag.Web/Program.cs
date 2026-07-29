using System.Net;
using LaserTag.Client;
using LaserTag.Client.Models;
using LaserTag.Game;
using LaserTag.Runtime;
using LaserTag.Ui;
using LaserTag.Web.Components;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);

// --broadcast <ip> overrides NIC discovery, matching the console host's flag
// (needed on machines with several adapters, e.g. Hyper-V/VMware virtual NICs).
IPEndPoint? broadcast = null;
int flag = Array.IndexOf(args, "--broadcast");
if (flag >= 0 && flag + 1 < args.Length && IPAddress.TryParse(args[flag + 1], out IPAddress? parsed))
{
    broadcast = new IPEndPoint(parsed, UdpTelemetryService.Port);
}

broadcast ??= BroadcastAddress.DiscoverLocalBroadcastEndpoint(UdpTelemetryService.Port)
    ?? new IPEndPoint(IPAddress.Broadcast, UdpTelemetryService.Port);

// Listen on all interfaces: the whole point is to open this from a phone on the
// same LAN, so binding to localhost would defeat the exercise. Plain HTTP only —
// a self-signed cert on a LAN IP just produces browser warnings on phones.
if (!builder.Environment.IsEnvironment("Testing"))
{
    builder.WebHost.UseUrls("http://0.0.0.0:5080");
}

builder.Services.AddRazorComponents().AddInteractiveServerComponents();

// Under test the UDP listener stays unstarted: SO_REUSEADDR lets several hosts
// bind 4210 without complaint, so parallel test hosts would quietly steal each
// other's datagrams.
builder.Services.AddLaserTagRuntime(
    broadcast,
    listen: !builder.Environment.IsEnvironment("Testing"));
builder.Services.AddSingleton<IGameSession>(sp => new LocalGameSession(
    sp.GetRequiredService<GameService>(),
    sp.GetRequiredService<UdpTelemetryService>(),
    broadcast.ToString()));

WebApplication app = builder.Build();

app.UseStaticFiles();
app.UseAntiforgery();
// AddAdditionalAssemblies is required as well as the Router's own
// AdditionalAssemblies: server-side endpoint routing is built from this list,
// and without it every shared screen 404s before the Router is ever consulted.
app.MapRazorComponents<App>()
    .AddInteractiveServerRenderMode()
    .AddAdditionalAssemblies(typeof(IGameSession).Assembly);

// A small JSON API beside the UI, for scripting and tests. The UI itself does
// not use it — Blazor Server calls the session directly — but having it keeps
// the manager automatable in the same spirit as the console REPL.
app.MapGet("/api/devices", (IGameSession session) => session.Devices().Select(d => new
{
    d.Id,
    Host = d.LastHeartbeat.Source,
    d.LastHeartbeat.Ip,
    d.LastHeartbeat.Team,
    d.LastHeartbeat.Hp,
    d.LastHeartbeat.Fw,
    d.Online,
}));

app.MapGet("/api/match", (IGameSession session) =>
{
    MatchSnapshot s = session.Snapshot();
    return Results.Ok(new
    {
        s.ModeName,
        Phase = s.Phase.ToString(),
        ElapsedSeconds = (int)s.Elapsed.TotalSeconds,
        RemainingSeconds = s.Remaining is { } r ? (int?)r.TotalSeconds : null,
        s.Winner,
        s.TeamScores,
        Players = s.Participants.Select(p => new { p.Id, p.Hostname, p.Team, p.Hp, p.Alive, p.Online }),
    });
});

app.MapPost("/api/match/start", (MatchRequest request, IGameSession session) =>
{
    if (!ModeFactory.TryCreate(request, out IGameMode mode, out string error))
    {
        return Results.BadRequest(new { error });
    }

    // A non-null return is the engine refusing (an empty lobby, or a match
    // already running) — a client error, not a server fault.
    string? refusal = session.StartMatch(mode);
    return refusal is null ? Results.Ok(new { ok = true }) : Results.BadRequest(new { error = refusal });
});

app.MapPost("/api/match/stop", (IGameSession session) =>
{
    session.Stop();
    return Results.Ok(new { ok = true });
});

app.MapPost("/api/team", async (TeamRequest request, IGameSession session) =>
{
    if (!Teams.IsValid(request.Team))
    {
        return Results.BadRequest(new { error = $"team must be 0-{Teams.Max} (0 = none)" });
    }

    string? error = await session.SetTeamAsync(request.Id, request.Team);
    return error is null ? Results.Ok(new { ok = true }) : Results.BadRequest(new { error });
});

app.MapPost("/api/control", (ControlRequest request, IGameSession session) =>
{
    if (!Enum.TryParse(request.Kind, ignoreCase: true, out ControlKind kind))
    {
        return Results.BadRequest(new { error = $"Unknown control kind '{request.Kind}'." });
    }

    session.SendControl(new Control { Kind = kind, Id = request.Id, T = request.T });
    return Results.Ok(new { ok = true });
});

app.Run();

/// <summary>A request to send one ad-hoc control verb.</summary>
/// <param name="Kind">The control kind, e.g. <c>reset</c> or <c>activate</c>.</param>
/// <param name="Id">Optional device id filter.</param>
/// <param name="T">Optional activate self-timeout in milliseconds.</param>
internal sealed record ControlRequest(string Kind, string? Id, int? T);

/// <summary>A request to assign one device's team.</summary>
/// <param name="Id">The device id.</param>
/// <param name="Team">The team: 0 for neutral, 1..4 for a side.</param>
internal sealed record TeamRequest(string Id, int Team);

/// <summary>Exposed so integration tests can construct a test host.</summary>
public partial class Program;
