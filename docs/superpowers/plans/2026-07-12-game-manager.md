# Game Manager (Spec A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** CTL grammar v2 + a host-side match engine (`LaserTag.Game`) with Deathmatch and Elimination modes, driven by a Generic Host console app (`LaserTag.Host`) with a UDP telemetry listener and the long-missing subnet-broadcast CTL sender.

**Architecture:** Pure-logic match engine (injectable clock, injected `IControlSender`, no sockets) fed parsed UDP messages plus timer ticks; game rules live in `IGameMode` plugins. A .NET Generic Host exe wires UDP 4210 telemetry → channel → engine, and a console REPL issues commands / prints the scoreboard. Wire-format changes go in the existing `LaserTag.Client` parser/formatter so firmware (Spec B) mirrors one definition.

**Tech Stack:** .NET 10 (`net10.0`, ImplicitUsings + Nullable enabled, `GenerateDocumentationFile` true), xUnit, Microsoft.Extensions.Hosting, Spectre.Console.

**Spec:** `docs/superpowers/specs/2026-07-12-game-manager-design.md` — read it before starting any task.

## Global Constraints

- All new projects: `net10.0`, `<ImplicitUsings>enable</ImplicitUsings>`, `<Nullable>enable</Nullable>`, `<GenerateDocumentationFile>true</GenerateDocumentationFile>`.
- **XML doc comments are required on every public API** (single-line `/// <summary>` OK when there are no params/returns; full tags when there are). Match the existing style in `LaserTag.Client`.
- Injectable clock everywhere time matters: `Func<DateTimeOffset>` ctor param, same pattern as `DeviceRoster`.
- Parser stays tolerant: malformed input → `null`, never throw (existing `UdpMessageParser` convention).
- UDP port **4210** for both telemetry and CTL. CTL goes to the **subnet broadcast** (e.g. `192.168.1.255`) — NEVER `255.255.255.255`.
- CTL sends repeat **3× by default, ~20 ms apart** (constructor-configurable).
- Defaults: `startHp` 32, countdown 5 s, Deathmatch hit +1 / kill +5 (kill shot scores both, 6 total), per-player respawn 10 s, waves off unless requested.
- Scoring is **per-team** (wire limitation; `EVT hit` has `shooterTeam` only).
- Run tests with `dotnet test dotnet/LaserTag.sln` from the repo root. Commit after every green task.
- Commit messages: imperative, no scope prefixes (match repo history, e.g. "Add SD card pins to BoardProfile").

---

### Task 1: CTL grammar v2 — model, formatter, parser

**Files:**
- Modify: `dotnet/LaserTag.Client/Models/UdpMessages.cs` (extend `ControlKind` + `Control`)
- Modify: `dotnet/LaserTag.Client/UdpMessageParser.cs` (extend `FormatControl`, add `ParseControl`)
- Test: `dotnet/LaserTag.Client.Tests/UdpMessageParserTests.cs` (append)

**Interfaces:**
- Consumes: existing `Control`, `ControlKind`, `UdpMessageParser`.
- Produces: `ControlKind.{Countdown,GameOver,Activate,Deactivate}`; `Control` gains `int? N`, `int? Winner`, `string? Id`; `string FormatControl(Control)` handles all seven kinds with `id=` appended last; `Control? ParseControl(string? line)` (round-trips everything `FormatControl` emits; unknown verb/malformed → null).

- [ ] **Step 1: Write the failing tests** (append to `UdpMessageParserTests.cs`)

```csharp
[Theory]
[InlineData(ControlKind.Countdown, null, null, 5, null, null, "CTL countdown n=5")]
[InlineData(ControlKind.GameOver, null, null, null, 2, null, "CTL gameover winner=2")]
[InlineData(ControlKind.GameOver, null, null, null, 0, null, "CTL gameover winner=0")]
[InlineData(ControlKind.Activate, null, null, null, null, "752b38", "CTL activate id=752b38")]
[InlineData(ControlKind.Deactivate, null, null, null, null, null, "CTL deactivate")]
[InlineData(ControlKind.Reset, null, 32, null, null, "752b38", "CTL reset hp=32 id=752b38")]
[InlineData(ControlKind.Start, 30000L, null, null, null, "752b38", "CTL start ts=30000 id=752b38")]
public void FormatControl_GrammarV2_EmitsGoldenStrings(
    ControlKind kind, long? ts, int? hp, int? n, int? winner, string? id, string expected)
{
    var parser = new UdpMessageParser();
    var control = new Control { Kind = kind, Ts = ts, Hp = hp, N = n, Winner = winner, Id = id };
    Assert.Equal(expected, parser.FormatControl(control));
}

[Theory]
[InlineData("CTL countdown n=5")]
[InlineData("CTL gameover winner=0")]
[InlineData("CTL activate id=752b38")]
[InlineData("CTL deactivate")]
[InlineData("CTL reset hp=32 id=752b38")]
[InlineData("CTL start")]
[InlineData("CTL stop")]
public void ParseControl_RoundTripsFormattedStrings(string wire)
{
    var parser = new UdpMessageParser();
    Control? parsed = parser.ParseControl(wire);
    Assert.NotNull(parsed);
    Assert.Equal(wire, parser.FormatControl(parsed));
}

[Theory]
[InlineData(null)]
[InlineData("")]
[InlineData("CTL")]
[InlineData("CTL warp")]
[InlineData("hostname CTL start")] // CTL lines carry no hostname prefix
[InlineData("CTL countdown n=abc")]
public void ParseControl_MalformedOrUnknown_ReturnsNull(string? wire)
{
    Assert.Null(new UdpMessageParser().ParseControl(wire));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `dotnet test dotnet/LaserTag.sln --filter "FullyQualifiedName~UdpMessageParserTests" 2>&1 | tail -20`
Expected: compile errors (`N`, `Winner`, `Id`, `Countdown` … not defined) — that counts as the failing state.

- [ ] **Step 3: Implement**

In `UdpMessages.cs`, extend the enum and record (keep the existing XML-doc style; new members documented):

```csharp
public enum ControlKind
{
    /// <summary>Simultaneous game start (<c>CTL start</c>).</summary>
    Start,

    /// <summary>Game stop (<c>CTL stop</c>).</summary>
    Stop,

    /// <summary>Force all devices to a state (<c>CTL reset</c>).</summary>
    Reset,

    /// <summary>Pre-match countdown cue (<c>CTL countdown</c>, grammar v2).</summary>
    Countdown,

    /// <summary>Match-end cue with winning team (<c>CTL gameover</c>, grammar v2).</summary>
    GameOver,

    /// <summary>Wake a dormant target (<c>CTL activate</c>, grammar v2).</summary>
    Activate,

    /// <summary>Send a target dormant (<c>CTL deactivate</c>, grammar v2).</summary>
    Deactivate,
}
```

Add to the `Control` record (below `Hp`):

```csharp
    /// <summary>
    /// Gets the optional countdown length in seconds. Valid for
    /// <see cref="ControlKind.Countdown"/>.
    /// </summary>
    public int? N { get; init; }

    /// <summary>
    /// Gets the optional winning team (<c>0</c> = draw). Valid for
    /// <see cref="ControlKind.GameOver"/>.
    /// </summary>
    public int? Winner { get; init; }

    /// <summary>
    /// Gets the optional target device id (grammar v2 addressing). When set, a
    /// device applies the CTL only if the id matches its own; valid on every kind.
    /// </summary>
    public string? Id { get; init; }
```

In `UdpMessageParser.FormatControl`, add the new cases before `default:` and append `id=` last (after the switch, before `return`):

```csharp
            case ControlKind.Countdown:
                sb.Append("countdown");
                if (control.N is { } n)
                {
                    sb.Append(" n=").Append(n.ToString(CultureInfo.InvariantCulture));
                }

                break;

            case ControlKind.GameOver:
                sb.Append("gameover");
                if (control.Winner is { } winner)
                {
                    sb.Append(" winner=").Append(winner.ToString(CultureInfo.InvariantCulture));
                }

                break;

            case ControlKind.Activate:
                sb.Append("activate");
                break;

            case ControlKind.Deactivate:
                sb.Append("deactivate");
                break;
```

```csharp
        // Grammar v2 addressing: id= is always the last key when present.
        if (control.Id is { } targetId)
        {
            sb.Append(" id=").Append(targetId);
        }

        return sb.ToString();
```

Add `ParseControl` after `FormatControl` (reuses the private `ParseFields`/`TryGetInt`/`TryGetLong` helpers):

```csharp
    /// <summary>
    /// Parses a host→device <c>CTL ...</c> wire line back into a
    /// <see cref="Control"/>. The inverse of <see cref="FormatControl"/>.
    /// </summary>
    /// <param name="line">The raw CTL line (no hostname prefix; trailing newline tolerated).</param>
    /// <returns>
    /// The parsed control, or <see langword="null"/> if the line is empty,
    /// malformed, has a hostname prefix, or names an unknown verb. Never throws.
    /// </returns>
    public Control? ParseControl(string? line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return null;
        }

        string[] tokens = line.Trim().Split(Whitespace, StringSplitOptions.RemoveEmptyEntries);
        if (tokens.Length < 2 || tokens[0] != "CTL")
        {
            return null;
        }

        ControlKind kind;
        switch (tokens[1])
        {
            case "start": kind = ControlKind.Start; break;
            case "stop": kind = ControlKind.Stop; break;
            case "reset": kind = ControlKind.Reset; break;
            case "countdown": kind = ControlKind.Countdown; break;
            case "gameover": kind = ControlKind.GameOver; break;
            case "activate": kind = ControlKind.Activate; break;
            case "deactivate": kind = ControlKind.Deactivate; break;
            default: return null;
        }

        Dictionary<string, string> fields = ParseFields(tokens, start: 2);

        long? ts = null;
        if (fields.ContainsKey("ts"))
        {
            if (!TryGetLong(fields, "ts", out long tsValue))
            {
                return null;
            }

            ts = tsValue;
        }

        int? hp = null, n = null, winner = null;
        if (fields.ContainsKey("hp"))
        {
            if (!TryGetInt(fields, "hp", out int v))
            {
                return null;
            }

            hp = v;
        }

        if (fields.ContainsKey("n"))
        {
            if (!TryGetInt(fields, "n", out int v))
            {
                return null;
            }

            n = v;
        }

        if (fields.ContainsKey("winner"))
        {
            if (!TryGetInt(fields, "winner", out int v))
            {
                return null;
            }

            winner = v;
        }

        fields.TryGetValue("id", out string? id);

        return new Control { Kind = kind, Ts = ts, Hp = hp, N = n, Winner = winner, Id = id };
    }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `dotnet test dotnet/LaserTag.sln 2>&1 | tail -5`
Expected: all pass (57 existing + the new ones), zero failures.

- [ ] **Step 5: Commit**

```bash
git add dotnet/LaserTag.Client dotnet/LaserTag.Client.Tests
git commit -m "Add CTL grammar v2: countdown/gameover/activate/deactivate + id= addressing"
```

---

### Task 2: UdpControlSender + broadcast-address helper

**Files:**
- Create: `dotnet/LaserTag.Client/IControlSender.cs`
- Create: `dotnet/LaserTag.Client/UdpControlSender.cs`
- Create: `dotnet/LaserTag.Client/BroadcastAddress.cs`
- Test: `dotnet/LaserTag.Client.Tests/UdpControlSenderTests.cs`

**Interfaces:**
- Consumes: `Control`, `UdpMessageParser.FormatControl` (Task 1).
- Produces: `interface IControlSender { Task SendAsync(Control control, CancellationToken cancellationToken = default); }`; `UdpControlSender(IPEndPoint broadcastEndpoint, int repeats = 3, TimeSpan? repeatGap = null)` + test ctor `UdpControlSender(Func<byte[], CancellationToken, Task> transmit, int repeats = 3, TimeSpan? repeatGap = null)`; `static IPAddress BroadcastAddress.Compute(IPAddress address, IPAddress mask)`; `static IPEndPoint? BroadcastAddress.DiscoverLocalBroadcastEndpoint(int port)`.

- [ ] **Step 1: Write the failing tests**

```csharp
using System.Net;
using System.Text;
using LaserTag.Client.Models;

namespace LaserTag.Client.Tests;

public class UdpControlSenderTests
{
    [Fact]
    public async Task SendAsync_RepeatsPayloadThreeTimesByDefault()
    {
        var sent = new List<string>();
        var sender = new UdpControlSender(
            (payload, _) =>
            {
                sent.Add(Encoding.ASCII.GetString(payload));
                return Task.CompletedTask;
            },
            repeatGap: TimeSpan.Zero);

        await sender.SendAsync(new Control { Kind = ControlKind.Stop });

        Assert.Equal(3, sent.Count);
        Assert.All(sent, s => Assert.Equal("CTL stop", s));
    }

    [Fact]
    public async Task SendAsync_HonoursConfiguredRepeatCount()
    {
        int count = 0;
        var sender = new UdpControlSender((_, _) => { count++; return Task.CompletedTask; }, repeats: 4, repeatGap: TimeSpan.Zero);

        await sender.SendAsync(new Control { Kind = ControlKind.Start });

        Assert.Equal(4, count);
    }

    [Theory]
    [InlineData("192.168.1.59", "255.255.255.0", "192.168.1.255")]
    [InlineData("10.20.30.40", "255.255.0.0", "10.20.255.255")]
    [InlineData("172.16.5.9", "255.255.255.128", "172.16.5.127")]
    public void Compute_DerivesSubnetBroadcast(string address, string mask, string expected)
    {
        IPAddress result = BroadcastAddress.Compute(IPAddress.Parse(address), IPAddress.Parse(mask));
        Assert.Equal(IPAddress.Parse(expected), result);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `dotnet test dotnet/LaserTag.sln --filter "FullyQualifiedName~UdpControlSenderTests" 2>&1 | tail -10`
Expected: compile errors (types don't exist).

- [ ] **Step 3: Implement**

`IControlSender.cs`:

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Client;

/// <summary>
/// Sends host→device <see cref="Control"/> messages. Implementations own the
/// transport (UDP subnet broadcast in production, in-memory fakes in tests).
/// </summary>
public interface IControlSender
{
    /// <summary>
    /// Sends a control message, repeating it per the implementation's
    /// reliability policy (CTL is lossy fire-and-forget UDP).
    /// </summary>
    /// <param name="control">The control message to send.</param>
    /// <param name="cancellationToken">Cancels the send (including repeats).</param>
    /// <returns>A task that completes when all repeats have been handed to the transport.</returns>
    Task SendAsync(Control control, CancellationToken cancellationToken = default);
}
```

`UdpControlSender.cs`:

```csharp
using System.Net;
using System.Net.Sockets;
using System.Text;
using LaserTag.Client.Models;

namespace LaserTag.Client;

/// <summary>
/// Sends <c>CTL</c> lines to the subnet broadcast address over UDP, repeating
/// each message to survive loss (contract: CTL is fire-and-forget). Never use
/// <c>255.255.255.255</c> — devices only receive subnet-directed broadcasts.
/// </summary>
public sealed class UdpControlSender : IControlSender, IDisposable
{
    private readonly UdpMessageParser _parser = new();
    private readonly Func<byte[], CancellationToken, Task> _transmit;
    private readonly int _repeats;
    private readonly TimeSpan _repeatGap;
    private readonly UdpClient? _client;

    /// <summary>
    /// Initializes a production sender that broadcasts to the given endpoint
    /// (subnet broadcast address, port 4210).
    /// </summary>
    /// <param name="broadcastEndpoint">The subnet broadcast endpoint, e.g. 192.168.1.255:4210.</param>
    /// <param name="repeats">How many times each CTL is sent. Defaults to 3.</param>
    /// <param name="repeatGap">Delay between repeats. Defaults to 20 ms.</param>
    public UdpControlSender(IPEndPoint broadcastEndpoint, int repeats = 3, TimeSpan? repeatGap = null)
        : this(repeats, repeatGap)
    {
        ArgumentNullException.ThrowIfNull(broadcastEndpoint);
        _client = new UdpClient { EnableBroadcast = true };
        _client.Connect(broadcastEndpoint);
        _transmit = async (payload, ct) => await _client.SendAsync(payload, ct).ConfigureAwait(false);
    }

    /// <summary>
    /// Initializes a sender with a custom transmit function — intended for
    /// tests, which capture payloads instead of opening a socket.
    /// </summary>
    /// <param name="transmit">Receives each raw payload exactly as it would hit the wire.</param>
    /// <param name="repeats">How many times each CTL is sent. Defaults to 3.</param>
    /// <param name="repeatGap">Delay between repeats. Defaults to 20 ms.</param>
    public UdpControlSender(Func<byte[], CancellationToken, Task> transmit, int repeats = 3, TimeSpan? repeatGap = null)
        : this(repeats, repeatGap)
    {
        ArgumentNullException.ThrowIfNull(transmit);
        _transmit = transmit;
    }

    private UdpControlSender(int repeats, TimeSpan? repeatGap)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(repeats, 1);
        _repeats = repeats;
        _repeatGap = repeatGap ?? TimeSpan.FromMilliseconds(20);
        _transmit = null!; // assigned by every public ctor
    }

    /// <inheritdoc/>
    public async Task SendAsync(Control control, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(control);
        byte[] payload = Encoding.ASCII.GetBytes(_parser.FormatControl(control));
        for (int i = 0; i < _repeats; i++)
        {
            if (i > 0 && _repeatGap > TimeSpan.Zero)
            {
                await Task.Delay(_repeatGap, cancellationToken).ConfigureAwait(false);
            }

            await _transmit(payload, cancellationToken).ConfigureAwait(false);
        }
    }

    /// <inheritdoc/>
    public void Dispose() => _client?.Dispose();
}
```

`BroadcastAddress.cs`:

```csharp
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace LaserTag.Client;

/// <summary>
/// Derives the IPv4 subnet-directed broadcast address (devices do not receive
/// <c>255.255.255.255</c>; CTL must target e.g. <c>192.168.1.255</c>).
/// </summary>
public static class BroadcastAddress
{
    /// <summary>
    /// Computes the subnet broadcast address for an IPv4 address + mask.
    /// </summary>
    /// <param name="address">A local IPv4 unicast address.</param>
    /// <param name="mask">The subnet mask for that address.</param>
    /// <returns>The subnet-directed broadcast address.</returns>
    public static IPAddress Compute(IPAddress address, IPAddress mask)
    {
        ArgumentNullException.ThrowIfNull(address);
        ArgumentNullException.ThrowIfNull(mask);
        byte[] addr = address.GetAddressBytes();
        byte[] m = mask.GetAddressBytes();
        var result = new byte[addr.Length];
        for (int i = 0; i < addr.Length; i++)
        {
            result[i] = (byte)(addr[i] | ~m[i]);
        }

        return new IPAddress(result);
    }

    /// <summary>
    /// Scans the machine's NICs for the first operational IPv4 interface with a
    /// private-range unicast address and returns its subnet broadcast endpoint.
    /// </summary>
    /// <param name="port">The UDP port to pair with the broadcast address (4210 for CTL).</param>
    /// <returns>The endpoint, or <see langword="null"/> if no suitable NIC was found.</returns>
    public static IPEndPoint? DiscoverLocalBroadcastEndpoint(int port)
    {
        foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (nic.OperationalStatus != OperationalStatus.Up ||
                nic.NetworkInterfaceType == NetworkInterfaceType.Loopback)
            {
                continue;
            }

            foreach (UnicastIPAddressInformation ua in nic.GetIPProperties().UnicastAddresses)
            {
                if (ua.Address.AddressFamily == AddressFamily.InterNetwork && ua.IPv4Mask is not null)
                {
                    return new IPEndPoint(Compute(ua.Address, ua.IPv4Mask), port);
                }
            }
        }

        return null;
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `dotnet test dotnet/LaserTag.sln 2>&1 | tail -5`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add dotnet/LaserTag.Client dotnet/LaserTag.Client.Tests
git commit -m "Add UdpControlSender (subnet broadcast, 3x repeat) and BroadcastAddress helper"
```

---

### Task 3: LaserTag.Game project — state types + mode contract

**Files:**
- Create: `dotnet/LaserTag.Game/LaserTag.Game.csproj`
- Create: `dotnet/LaserTag.Game/MatchPhase.cs`
- Create: `dotnet/LaserTag.Game/Participant.cs`
- Create: `dotnet/LaserTag.Game/MatchSnapshot.cs`
- Create: `dotnet/LaserTag.Game/MatchResult.cs`
- Create: `dotnet/LaserTag.Game/MatchContext.cs`
- Create: `dotnet/LaserTag.Game/IGameMode.cs`
- Create: `dotnet/LaserTag.Game.Tests/LaserTag.Game.Tests.csproj`
- Modify: `dotnet/LaserTag.sln` (add both projects via `dotnet sln`)

**Interfaces:**
- Consumes: `LaserTag.Client` (`Control`, `HitEvent`, `StateEvent`, `Heartbeat`).
- Produces (exact shapes below — later tasks depend on them verbatim): `MatchPhase { Lobby, Countdown, Running, Finished }`; `Participant` record; `MatchSnapshot` record; `MatchResult(int WinnerTeam)` (0 = draw); `MatchContext` with `Now`, `MatchStartedAt`, `StartHp`, `Participants`, `Scores`, `AddScore(int, int)`, `Send(Control)`; `IGameMode` with `Name`, `OnMatchStart/OnHit/OnDeviceState/OnTick`, `MatchResult? CheckEnd(MatchContext)`.

- [ ] **Step 1: Create the projects and wire the solution**

```bash
cd dotnet
dotnet new classlib -n LaserTag.Game -f net10.0
rm LaserTag.Game/Class1.cs
dotnet new xunit -n LaserTag.Game.Tests -f net10.0
rm LaserTag.Game.Tests/UnitTest1.cs
dotnet add LaserTag.Game reference LaserTag.Client
dotnet add LaserTag.Game.Tests reference LaserTag.Game
dotnet sln add LaserTag.Game LaserTag.Game.Tests
```

Then edit `LaserTag.Game/LaserTag.Game.csproj` so its `<PropertyGroup>` matches `LaserTag.Client.csproj` exactly (add `<GenerateDocumentationFile>true</GenerateDocumentationFile>`).

- [ ] **Step 2: Write a smoke test** (`dotnet/LaserTag.Game.Tests/StateTypeTests.cs`)

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

public class StateTypeTests
{
    [Fact]
    public void MatchContext_AddScore_AccumulatesPerTeam()
    {
        var sent = new List<Control>();
        var scores = new Dictionary<int, int>();
        var ctx = new MatchContext(
            now: DateTimeOffset.UnixEpoch,
            matchStartedAt: DateTimeOffset.UnixEpoch,
            startHp: 32,
            participants: [],
            scores: scores,
            addScore: (team, pts) => scores[team] = scores.GetValueOrDefault(team) + pts,
            send: sent.Add);

        ctx.AddScore(2, 1);
        ctx.AddScore(2, 5);
        ctx.Send(new Control { Kind = ControlKind.Stop });

        Assert.Equal(6, ctx.Scores[2]);
        Assert.Single(sent);
    }
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `dotnet test dotnet/LaserTag.sln --filter "FullyQualifiedName~StateTypeTests" 2>&1 | tail -10`
Expected: compile errors.

- [ ] **Step 4: Implement the types**

`MatchPhase.cs`:

```csharp
namespace LaserTag.Game;

/// <summary>The lifecycle phase of a match (spec: Lobby → Countdown → Running → Finished).</summary>
public enum MatchPhase
{
    /// <summary>No match active; devices idle.</summary>
    Lobby,

    /// <summary>Countdown cue sent; match starts when it elapses.</summary>
    Countdown,

    /// <summary>Match in progress; events are scored.</summary>
    Running,

    /// <summary>Match over; scoreboard frozen until the next start.</summary>
    Finished,
}
```

`Participant.cs`:

```csharp
namespace LaserTag.Game;

/// <summary>
/// A device participating in the current match, as derived from the telemetry
/// stream. Hp here mirrors the device-authoritative value; the engine never
/// pushes hp, it only observes.
/// </summary>
public sealed record Participant
{
    /// <summary>Gets the device id (stable, from heartbeats).</summary>
    public required string Id { get; init; }

    /// <summary>Gets the device hostname (e.g. <c>lasertag-matrix</c>).</summary>
    public required string Hostname { get; init; }

    /// <summary>Gets the team index the device reported at lobby time.</summary>
    public required int Team { get; init; }

    /// <summary>Gets the last observed hp.</summary>
    public required int Hp { get; init; }

    /// <summary>Gets a value indicating whether the participant is alive (hp &gt; 0).</summary>
    public required bool Alive { get; init; }

    /// <summary>Gets a value indicating whether the device is currently online.</summary>
    public required bool Online { get; init; }

    /// <summary>Gets the time of death, when dead; used by respawn policies.</summary>
    public DateTimeOffset? DiedAt { get; init; }
}
```

`MatchResult.cs`:

```csharp
namespace LaserTag.Game;

/// <summary>A finished match's outcome.</summary>
/// <param name="WinnerTeam">The winning team, or <c>0</c> for a draw.</param>
public sealed record MatchResult(int WinnerTeam);
```

`MatchSnapshot.cs`:

```csharp
namespace LaserTag.Game;

/// <summary>An immutable snapshot of match state for display (scoreboard/UI).</summary>
public sealed record MatchSnapshot
{
    /// <summary>Gets the current phase.</summary>
    public required MatchPhase Phase { get; init; }

    /// <summary>Gets the active mode's display name, or empty in Lobby.</summary>
    public required string ModeName { get; init; }

    /// <summary>Gets the participants (order unspecified).</summary>
    public required IReadOnlyList<Participant> Participants { get; init; }

    /// <summary>Gets the per-team scores.</summary>
    public required IReadOnlyDictionary<int, int> TeamScores { get; init; }

    /// <summary>Gets time elapsed since the match started running (zero before Running).</summary>
    public required TimeSpan Elapsed { get; init; }

    /// <summary>Gets time remaining for timed modes, or <see langword="null"/> when untimed.</summary>
    public TimeSpan? Remaining { get; init; }

    /// <summary>Gets the winner once <see cref="Phase"/> is Finished (<c>0</c> = draw).</summary>
    public int? Winner { get; init; }
}
```

`MatchContext.cs`:

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// The engine-provided view a game mode operates on: current time, match
/// state, and outbound-control/scoring callbacks. Constructed by the engine
/// per delegated call; modes must not retain it across calls.
/// </summary>
public sealed class MatchContext
{
    private readonly Action<int, int> _addScore;
    private readonly Action<Control> _send;

    /// <summary>Initializes the context (engine-internal; public for tests).</summary>
    /// <param name="now">The current time per the engine's clock.</param>
    /// <param name="matchStartedAt">When the Running phase began.</param>
    /// <param name="startHp">The hp participants (re)spawn with.</param>
    /// <param name="participants">The current participants.</param>
    /// <param name="scores">The current per-team scores.</param>
    /// <param name="addScore">Callback adding points to a team.</param>
    /// <param name="send">Callback queueing an outbound control message.</param>
    public MatchContext(
        DateTimeOffset now,
        DateTimeOffset matchStartedAt,
        int startHp,
        IReadOnlyList<Participant> participants,
        IReadOnlyDictionary<int, int> scores,
        Action<int, int> addScore,
        Action<Control> send)
    {
        Now = now;
        MatchStartedAt = matchStartedAt;
        StartHp = startHp;
        Participants = participants;
        Scores = scores;
        _addScore = addScore;
        _send = send;
    }

    /// <summary>Gets the current time (injected clock — never read the system clock in a mode).</summary>
    public DateTimeOffset Now { get; }

    /// <summary>Gets when the Running phase began.</summary>
    public DateTimeOffset MatchStartedAt { get; }

    /// <summary>Gets the hp value participants (re)spawn with.</summary>
    public int StartHp { get; }

    /// <summary>Gets the current participants.</summary>
    public IReadOnlyList<Participant> Participants { get; }

    /// <summary>Gets the current per-team scores.</summary>
    public IReadOnlyDictionary<int, int> Scores { get; }

    /// <summary>Adds points to a team's score.</summary>
    /// <param name="team">The team index.</param>
    /// <param name="points">Points to add.</param>
    public void AddScore(int team, int points) => _addScore(team, points);

    /// <summary>Queues an outbound control message (sent via the engine's <c>IControlSender</c>).</summary>
    /// <param name="control">The control to send.</param>
    public void Send(Control control) => _send(control);
}
```

`IGameMode.cs`:

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// A pluggable game mode: the engine owns lifecycle/state mirroring, the mode
/// owns rules — scoring, respawn policy, and the win condition.
/// </summary>
public interface IGameMode
{
    /// <summary>Gets the display name (e.g. <c>deathmatch</c>).</summary>
    string Name { get; }

    /// <summary>Gets the fixed match length, or <see langword="null"/> for untimed modes.</summary>
    TimeSpan? MatchDuration { get; }

    /// <summary>Called once when the Running phase begins.</summary>
    /// <param name="context">The match context.</param>
    void OnMatchStart(MatchContext context);

    /// <summary>
    /// Called for each hit during Running, after the engine has updated the
    /// victim's hp/alive state (so <c>hit.Hp == 0</c> means this shot killed).
    /// </summary>
    /// <param name="context">The match context.</param>
    /// <param name="hit">The hit event.</param>
    void OnHit(MatchContext context, HitEvent hit);

    /// <summary>Called for each device state event during Running.</summary>
    /// <param name="context">The match context.</param>
    /// <param name="state">The state event.</param>
    /// <param name="participant">The participant it came from.</param>
    void OnDeviceState(MatchContext context, StateEvent state, Participant participant);

    /// <summary>Called on every engine tick during Running (respawn scheduling etc.).</summary>
    /// <param name="context">The match context.</param>
    void OnTick(MatchContext context);

    /// <summary>
    /// Checks the win condition. Called after every event and tick.
    /// </summary>
    /// <param name="context">The match context.</param>
    /// <returns>The result when the match should end, else <see langword="null"/>.</returns>
    MatchResult? CheckEnd(MatchContext context);
}
```

- [ ] **Step 5: Run tests, then commit**

Run: `dotnet test dotnet/LaserTag.sln 2>&1 | tail -5` — expected: all pass.

```bash
git add dotnet/LaserTag.Game dotnet/LaserTag.Game.Tests dotnet/LaserTag.sln
git commit -m "Add LaserTag.Game project: match state types and IGameMode contract"
```

---

### Task 4: MatchEngine — lifecycle (lobby, countdown, start, stop, snapshot)

**Files:**
- Create: `dotnet/LaserTag.Game/MatchEngine.cs`
- Test: `dotnet/LaserTag.Game.Tests/MatchEngineLifecycleTests.cs`
- Test helper: `dotnet/LaserTag.Game.Tests/TestHelpers.cs`

**Interfaces:**
- Consumes: Task 3 types; `IControlSender` (Task 2); `Heartbeat`/`UdpInboundMessage` (existing).
- Produces: `MatchEngine(IControlSender sender, Func<DateTimeOffset> clock, int countdownSeconds = 5, int startHp = 32)`; `MatchPhase Phase { get; }`; `void StartMatch(IGameMode mode, IEnumerable<Heartbeat> lobbyDevices)`; `void Stop()`; `void Tick()`; `void OnMessage(UdpInboundMessage message)` (Task 5 fills its body); `MatchSnapshot Snapshot()`.

- [ ] **Step 1: Write the test helpers** (`TestHelpers.cs`)

```csharp
using LaserTag.Client;
using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

/// <summary>Captures every control the engine sends, in order.</summary>
public sealed class FakeControlSender : IControlSender
{
    public List<Control> Sent { get; } = [];

    public Task SendAsync(Control control, CancellationToken cancellationToken = default)
    {
        Sent.Add(control);
        return Task.CompletedTask;
    }
}

/// <summary>A manually advanced clock.</summary>
public sealed class FakeClock
{
    public DateTimeOffset Now { get; private set; } = DateTimeOffset.UnixEpoch;

    public void Advance(TimeSpan by) => Now += by;
}

public static class Msg
{
    public static Heartbeat Hb(string id, int team, int hp = 32, string host = "host") => new()
    {
        Source = host, Id = id, Ip = "192.168.1.10", Fw = "2.0.0",
        Team = team, Mode = "idle", Hp = hp, Online = true,
    };

    public static HitEvent Hit(string victim, int shooterTeam, int dmg, int hpAfter, string host = "host") => new()
    {
        Source = host, Victim = victim, ShooterTeam = shooterTeam,
        Dmg = dmg, Proto = "vatos", Hp = hpAfter, Ts = 1000,
    };
}

/// <summary>A do-nothing mode for lifecycle tests.</summary>
public sealed class NullMode : IGameMode
{
    public string Name => "null";

    public TimeSpan? MatchDuration => null;

    public void OnMatchStart(MatchContext context)
    {
    }

    public void OnHit(MatchContext context, HitEvent hit)
    {
    }

    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant)
    {
    }

    public void OnTick(MatchContext context)
    {
    }

    public MatchResult? CheckEnd(MatchContext context) => null;
}
```

- [ ] **Step 2: Write the failing lifecycle tests** (`MatchEngineLifecycleTests.cs`)

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

public class MatchEngineLifecycleTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();

    private MatchEngine NewEngine() => new(_sender, () => _clock.Now);

    [Fact]
    public void StartMatch_EntersCountdown_AndSendsCountdownCue()
    {
        MatchEngine engine = NewEngine();

        engine.StartMatch(new NullMode(), [Msg.Hb("a", team: 1), Msg.Hb("b", team: 2)]);

        Assert.Equal(MatchPhase.Countdown, engine.Phase);
        Control cue = Assert.Single(_sender.Sent);
        Assert.Equal(ControlKind.Countdown, cue.Kind);
        Assert.Equal(5, cue.N);
        Assert.Equal(2, engine.Snapshot().Participants.Count);
    }

    [Fact]
    public void Tick_AfterCountdownElapses_StartsRunning_SendsStartAndReset()
    {
        MatchEngine engine = NewEngine();
        engine.StartMatch(new NullMode(), [Msg.Hb("a", 1)]);

        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();

        Assert.Equal(MatchPhase.Running, engine.Phase);
        Assert.Equal(ControlKind.Start, _sender.Sent[1].Kind);
        Assert.Equal(ControlKind.Reset, _sender.Sent[2].Kind);
        Assert.Equal(32, _sender.Sent[2].Hp);
        Assert.All(engine.Snapshot().Participants, p => Assert.True(p.Alive && p.Hp == 32));
    }

    [Fact]
    public void Stop_DuringRunning_FinishesWithModeResultOrDraw()
    {
        MatchEngine engine = NewEngine();
        engine.StartMatch(new NullMode(), [Msg.Hb("a", 1)]);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();

        engine.Stop();

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Control last = _sender.Sent[^1];
        Assert.Equal(ControlKind.GameOver, last.Kind);
        Assert.Equal(0, last.Winner); // NullMode never yields a result → draw
        Assert.Equal(0, engine.Snapshot().Winner);
    }

    [Fact]
    public void StartMatch_WhenNotInLobbyOrFinished_Throws()
    {
        MatchEngine engine = NewEngine();
        engine.StartMatch(new NullMode(), [Msg.Hb("a", 1)]);

        Assert.Throws<InvalidOperationException>(
            () => engine.StartMatch(new NullMode(), [Msg.Hb("a", 1)]));
    }

    [Fact]
    public void StartMatch_WithNoDevices_Throws()
    {
        Assert.Throws<InvalidOperationException>(() => NewEngine().StartMatch(new NullMode(), []));
    }
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `dotnet test dotnet/LaserTag.sln --filter "FullyQualifiedName~MatchEngineLifecycleTests" 2>&1 | tail -10`
Expected: compile errors (`MatchEngine` missing).

- [ ] **Step 4: Implement `MatchEngine.cs`**

```csharp
using LaserTag.Client;
using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// The host-side match orchestrator: owns the match lifecycle
/// (Lobby → Countdown → Running → Finished), mirrors device state from the
/// telemetry stream, delegates rules to the active <see cref="IGameMode"/>,
/// and emits CTL messages via an injected <see cref="IControlSender"/>.
/// Single-threaded by design — callers serialize access (the host app uses a
/// lock; tests are naturally sequential).
/// </summary>
public sealed class MatchEngine
{
    private readonly IControlSender _sender;
    private readonly Func<DateTimeOffset> _clock;
    private readonly int _countdownSeconds;
    private readonly int _startHp;
    private readonly Dictionary<string, Participant> _participants = new(StringComparer.Ordinal);
    private readonly Dictionary<int, int> _scores = [];

    private IGameMode? _mode;
    private DateTimeOffset _countdownEndsAt;
    private DateTimeOffset _matchStartedAt;
    private int? _winner;

    /// <summary>Initializes a new engine.</summary>
    /// <param name="sender">The outbound control transport.</param>
    /// <param name="clock">The injectable time source (tests pass a fake).</param>
    /// <param name="countdownSeconds">Pre-match countdown length. Defaults to 5.</param>
    /// <param name="startHp">Hp participants (re)spawn with. Defaults to 32.</param>
    public MatchEngine(IControlSender sender, Func<DateTimeOffset> clock, int countdownSeconds = 5, int startHp = 32)
    {
        ArgumentNullException.ThrowIfNull(sender);
        ArgumentNullException.ThrowIfNull(clock);
        _sender = sender;
        _clock = clock;
        _countdownSeconds = countdownSeconds;
        _startHp = startHp;
    }

    /// <summary>Gets the current lifecycle phase.</summary>
    public MatchPhase Phase { get; private set; } = MatchPhase.Lobby;

    /// <summary>
    /// Snapshots the lobby (participants = the given heartbeats), sends the
    /// countdown cue, and enters the Countdown phase.
    /// </summary>
    /// <param name="mode">The game mode that will govern the match.</param>
    /// <param name="lobbyDevices">Heartbeats of the online devices to enroll.</param>
    /// <exception cref="InvalidOperationException">
    /// Thrown if a match is already in progress or no devices were supplied.
    /// </exception>
    public void StartMatch(IGameMode mode, IEnumerable<Heartbeat> lobbyDevices)
    {
        ArgumentNullException.ThrowIfNull(mode);
        ArgumentNullException.ThrowIfNull(lobbyDevices);
        if (Phase is MatchPhase.Countdown or MatchPhase.Running)
        {
            throw new InvalidOperationException($"Cannot start a match while phase is {Phase}.");
        }

        _participants.Clear();
        _scores.Clear();
        _winner = null;
        foreach (Heartbeat hb in lobbyDevices)
        {
            _participants[hb.Id] = new Participant
            {
                Id = hb.Id,
                Hostname = hb.Source,
                Team = hb.Team,
                Hp = hb.Hp,
                Alive = hb.Hp > 0,
                Online = true,
            };
        }

        if (_participants.Count == 0)
        {
            throw new InvalidOperationException("Cannot start a match with no devices in the lobby.");
        }

        _mode = mode;
        Phase = MatchPhase.Countdown;
        _countdownEndsAt = _clock() + TimeSpan.FromSeconds(_countdownSeconds);
        Send(new Control { Kind = ControlKind.Countdown, N = _countdownSeconds });
    }

    /// <summary>
    /// Ends the match immediately: the mode's current result decides the
    /// winner (draw if the mode has none yet). No-op outside Countdown/Running.
    /// </summary>
    public void Stop()
    {
        if (Phase is not (MatchPhase.Countdown or MatchPhase.Running))
        {
            return;
        }

        MatchResult? result = Phase == MatchPhase.Running ? _mode!.CheckEnd(Context()) : null;
        Finish(result?.WinnerTeam ?? 0);
    }

    /// <summary>
    /// Advances time-driven behaviour: countdown expiry, mode ticks
    /// (respawn scheduling), and the win-condition check. Call ~every 250 ms.
    /// </summary>
    public void Tick()
    {
        DateTimeOffset now = _clock();
        if (Phase == MatchPhase.Countdown && now >= _countdownEndsAt)
        {
            BeginRunning(now);
        }

        if (Phase == MatchPhase.Running)
        {
            MatchContext ctx = Context();
            _mode!.OnTick(ctx);
            CheckEnd(ctx);
        }
    }

    /// <summary>
    /// Feeds a parsed telemetry message into the engine (hit/state/heartbeat
    /// handling — see the event-handling section of the spec).
    /// </summary>
    /// <param name="message">The parsed inbound message.</param>
    public void OnMessage(UdpInboundMessage message)
    {
        ArgumentNullException.ThrowIfNull(message);

        // Filled in by the event-handling task.
    }

    /// <summary>Takes an immutable snapshot for display.</summary>
    /// <returns>The snapshot.</returns>
    public MatchSnapshot Snapshot()
    {
        DateTimeOffset now = _clock();
        TimeSpan elapsed = Phase is MatchPhase.Running or MatchPhase.Finished
            ? now - _matchStartedAt
            : TimeSpan.Zero;
        return new MatchSnapshot
        {
            Phase = Phase,
            ModeName = _mode?.Name ?? string.Empty,
            Participants = [.. _participants.Values],
            TeamScores = new Dictionary<int, int>(_scores),
            Elapsed = elapsed,
            Remaining = _mode?.MatchDuration is { } d && Phase == MatchPhase.Running
                ? d - elapsed
                : null,
            Winner = _winner,
        };
    }

    private void BeginRunning(DateTimeOffset now)
    {
        Phase = MatchPhase.Running;
        _matchStartedAt = now;
        Send(new Control { Kind = ControlKind.Start });
        Send(new Control { Kind = ControlKind.Reset, Hp = _startHp });
        foreach (string id in _participants.Keys.ToList())
        {
            _participants[id] = _participants[id] with { Hp = _startHp, Alive = true, DiedAt = null };
        }

        _mode!.OnMatchStart(Context());
    }

    private void Finish(int winnerTeam)
    {
        Phase = MatchPhase.Finished;
        _winner = winnerTeam;
        Send(new Control { Kind = ControlKind.GameOver, Winner = winnerTeam });
    }

    private void CheckEnd(MatchContext ctx)
    {
        if (Phase == MatchPhase.Running && _mode!.CheckEnd(ctx) is { } result)
        {
            Finish(result.WinnerTeam);
        }
    }

    private MatchContext Context() => new(
        now: _clock(),
        matchStartedAt: _matchStartedAt,
        startHp: _startHp,
        participants: [.. _participants.Values],
        scores: new Dictionary<int, int>(_scores),
        addScore: (team, pts) => _scores[team] = _scores.GetValueOrDefault(team) + pts,
        send: Send);

    private void Send(Control control) =>

        // CTL is fire-and-forget (repeats handled by the sender); the engine
        // stays synchronous, so sends are intentionally not awaited.
        _ = _sender.SendAsync(control);
}
```

Note: the fake sender in tests completes synchronously, so `Sent` ordering is deterministic despite the fire-and-forget.

- [ ] **Step 5: Run tests, then commit**

Run: `dotnet test dotnet/LaserTag.sln 2>&1 | tail -5` — expected: all pass.

```bash
git add dotnet/LaserTag.Game dotnet/LaserTag.Game.Tests
git commit -m "Add MatchEngine lifecycle: lobby snapshot, countdown, start/stop, scoreboard snapshot"
```

---

### Task 5: MatchEngine — event handling (hits, state, HB reconciliation, rejoin)

**Files:**
- Modify: `dotnet/LaserTag.Game/MatchEngine.cs` (fill `OnMessage`)
- Test: `dotnet/LaserTag.Game.Tests/MatchEngineEventTests.cs`

**Interfaces:**
- Consumes: Task 4's `MatchEngine`; `Msg`/`FakeControlSender`/`FakeClock`/`NullMode` helpers.
- Produces: `OnMessage` behaviour — during Running: `HitEvent` updates victim hp/alive/DiedAt then calls `mode.OnHit` then `CheckEnd`; `StateEvent` updates hp when present and calls `mode.OnDeviceState`; `Heartbeat` reconciles hp/alive/online (no scoring) and re-issues `CTL start id=<id>` to a device that rejoins after being offline. Outside Running, messages only update online/hp mirrors (no mode calls).

- [ ] **Step 1: Write the failing tests** (`MatchEngineEventTests.cs`)

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

/// <summary>Records mode callback invocations.</summary>
public sealed class RecordingMode : IGameMode
{
    public List<HitEvent> Hits { get; } = [];

    public List<StateEvent> States { get; } = [];

    public string Name => "recording";

    public TimeSpan? MatchDuration => null;

    public void OnMatchStart(MatchContext context)
    {
    }

    public void OnHit(MatchContext context, HitEvent hit) => Hits.Add(hit);

    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant) => States.Add(state);

    public void OnTick(MatchContext context)
    {
    }

    public MatchResult? CheckEnd(MatchContext context) => null;
}

public class MatchEngineEventTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();
    private readonly RecordingMode _mode = new();

    private MatchEngine RunningEngine(params Heartbeat[] lobby)
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);
        engine.StartMatch(_mode, lobby);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();
        _sender.Sent.Clear();
        return engine;
    }

    [Fact]
    public void Hit_UpdatesVictimAndNotifiesMode()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.OnMessage(Msg.Hit(victim: "a", shooterTeam: 2, dmg: 2, hpAfter: 30));

        Participant a = engine.Snapshot().Participants.Single(p => p.Id == "a");
        Assert.Equal(30, a.Hp);
        Assert.True(a.Alive);
        Assert.Single(_mode.Hits);
    }

    [Fact]
    public void FatalHit_MarksDeadWithDiedAt()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0));

        Participant a = engine.Snapshot().Participants.Single(p => p.Id == "a");
        Assert.False(a.Alive);
        Assert.Equal(_clock.Now, a.DiedAt);
    }

    [Fact]
    public void Hit_FromUnknownDevice_IsIgnored()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1));

        engine.OnMessage(Msg.Hit("ghost", 2, 2, 0));

        Assert.Empty(_mode.Hits);
    }

    [Fact]
    public void Hit_OutsideRunning_IsIgnored()
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);

        engine.OnMessage(Msg.Hit("a", 2, 2, 0)); // Lobby phase

        Assert.Empty(_mode.Hits);
        Assert.Equal(MatchPhase.Lobby, engine.Phase);
    }

    [Fact]
    public void Heartbeat_ReconcilesHpDrop_WithoutScoring()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));

        // The EVT hit was lost; the next HB shows hp=0.
        engine.OnMessage(Msg.Hb("a", 1, hp: 0));

        Participant a = engine.Snapshot().Participants.Single(p => p.Id == "a");
        Assert.False(a.Alive);
        Assert.Empty(_mode.Hits); // reconciliation never scores
    }

    [Fact]
    public void Heartbeat_AfterOffline_ReissuesStartToThatDevice()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.MarkOffline("a"); // roster says it dropped
        engine.OnMessage(Msg.Hb("a", 1, hp: 32));

        Control reissue = Assert.Single(_sender.Sent);
        Assert.Equal(ControlKind.Start, reissue.Kind);
        Assert.Equal("a", reissue.Id);
        Assert.True(engine.Snapshot().Participants.Single(p => p.Id == "a").Online);
    }

    [Fact]
    public void StateEvent_UpdatesHpAndNotifiesMode()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1, host: "lasertag-a"));

        engine.OnMessage(new StateEvent { Source = "lasertag-a", S = "respawn", Hp = 32, Ts = 2000 });

        Assert.Single(_mode.States);
        Assert.Equal(32, engine.Snapshot().Participants.Single().Hp);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `dotnet test dotnet/LaserTag.sln --filter "FullyQualifiedName~MatchEngineEventTests" 2>&1 | tail -10`
Expected: compile error (`MarkOffline` missing) and/or failures — `OnMessage` is currently a stub.

- [ ] **Step 3: Implement**

Replace the `OnMessage` stub body and add `MarkOffline` + private handlers to `MatchEngine`:

```csharp
    /// <summary>
    /// Feeds a parsed telemetry message into the engine. Hits and state events
    /// are delegated to the mode only while Running; heartbeats always refresh
    /// the hp/online mirror (and trigger a re-issued <c>CTL start id=</c> when
    /// a participant rejoins mid-match).
    /// </summary>
    /// <param name="message">The parsed inbound message.</param>
    public void OnMessage(UdpInboundMessage message)
    {
        ArgumentNullException.ThrowIfNull(message);
        switch (message)
        {
            case HitEvent hit:
                OnHit(hit);
                break;
            case StateEvent state:
                OnState(state);
                break;
            case Heartbeat hb:
                OnHeartbeat(hb);
                break;
        }
    }

    /// <summary>
    /// Marks a participant offline (driven by the host's <c>DeviceRoster</c>
    /// liveness timeouts — the engine has no timeout logic of its own).
    /// </summary>
    /// <param name="deviceId">The device id.</param>
    public void MarkOffline(string deviceId)
    {
        if (_participants.TryGetValue(deviceId, out Participant? p))
        {
            _participants[deviceId] = p with { Online = false };
        }
    }

    private void OnHit(HitEvent hit)
    {
        if (Phase != MatchPhase.Running || !_participants.TryGetValue(hit.Victim, out Participant? victim))
        {
            return;
        }

        bool died = hit.Hp <= 0 && victim.Alive;
        _participants[hit.Victim] = victim with
        {
            Hp = hit.Hp,
            Alive = hit.Hp > 0,
            DiedAt = died ? _clock() : victim.DiedAt,
        };

        MatchContext ctx = Context();
        _mode!.OnHit(ctx, hit);
        CheckEnd(ctx);
    }

    private void OnState(StateEvent state)
    {
        // State events carry the hostname, not the device id — resolve by hostname.
        Participant? participant = _participants.Values.FirstOrDefault(p => p.Hostname == state.Source);
        if (participant is null)
        {
            return;
        }

        if (state.Hp is { } hp)
        {
            _participants[participant.Id] = participant with
            {
                Hp = hp,
                Alive = hp > 0,
                DiedAt = hp > 0 ? null : participant.DiedAt,
            };
        }

        if (Phase == MatchPhase.Running)
        {
            MatchContext ctx = Context();
            _mode!.OnDeviceState(ctx, state, _participants[participant.Id]);
            CheckEnd(ctx);
        }
    }

    private void OnHeartbeat(Heartbeat hb)
    {
        if (!_participants.TryGetValue(hb.Id, out Participant? p))
        {
            return; // Not enrolled in this match; the lobby is fixed at start.
        }

        bool rejoined = !p.Online && Phase == MatchPhase.Running;

        // Reconcile the authoritative hp from the heartbeat: covers lost EVT
        // packets. Never scores (shooter unknown) — spec "unattributed hit".
        bool died = hb.Hp <= 0 && p.Alive;
        _participants[hb.Id] = p with
        {
            Hp = hb.Hp,
            Alive = hb.Hp > 0,
            Online = true,
            DiedAt = died ? _clock() : (hb.Hp > 0 ? null : p.DiedAt),
        };

        if (rejoined)
        {
            Send(new Control { Kind = ControlKind.Start, Id = hb.Id });
        }

        if (Phase == MatchPhase.Running)
        {
            CheckEnd(Context());
        }
    }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `dotnet test dotnet/LaserTag.sln 2>&1 | tail -5`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add dotnet/LaserTag.Game dotnet/LaserTag.Game.Tests
git commit -m "MatchEngine event handling: hits, state, HB hp-reconciliation, rejoin re-issue"
```

---

### Task 6: DeathmatchMode

**Files:**
- Create: `dotnet/LaserTag.Game/DeathmatchMode.cs`
- Test: `dotnet/LaserTag.Game.Tests/DeathmatchModeTests.cs`

**Interfaces:**
- Consumes: `IGameMode`, `MatchContext`, `MatchResult`, `Participant`, `Control` (Tasks 1/3), engine behaviour (Tasks 4/5).
- Produces: `DeathmatchMode(TimeSpan duration, int hitPoints = 1, int killPoints = 5, TimeSpan? respawnDelay = null, TimeSpan? waveInterval = null)` — `respawnDelay` defaults to 10 s when `waveInterval` is null; passing `waveInterval` switches to wave respawns (per-id resets for currently-dead participants each wave; a broadcast reset would heal alive players, which changes gameplay — deliberate deviation from the spec's word "broadcast", same observable effect for dead players). `Name == "deathmatch"`.

- [ ] **Step 1: Write the failing tests**

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

public class DeathmatchModeTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();

    private MatchEngine Running(DeathmatchMode mode, params Heartbeat[] lobby)
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);
        engine.StartMatch(mode, lobby);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();
        _sender.Sent.Clear();
        return engine;
    }

    [Fact]
    public void Hit_ScoresHitPoints_KillScoresBoth()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(5));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.OnMessage(Msg.Hit("a", shooterTeam: 2, dmg: 2, hpAfter: 30));
        engine.OnMessage(Msg.Hit("a", shooterTeam: 2, dmg: 2, hpAfter: 0)); // kill

        Assert.Equal(1 + (1 + 5), engine.Snapshot().TeamScores[2]);
    }

    [Fact]
    public void DeadPlayer_RespawnsAfterDelay_ViaAddressedReset()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(5), respawnDelay: TimeSpan.FromSeconds(10));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0));

        _clock.Advance(TimeSpan.FromSeconds(9));
        engine.Tick();
        Assert.DoesNotContain(_sender.Sent, c => c.Kind == ControlKind.Reset);

        _clock.Advance(TimeSpan.FromSeconds(1));
        engine.Tick();

        Control reset = Assert.Single(_sender.Sent, c => c.Kind == ControlKind.Reset);
        Assert.Equal("a", reset.Id);
        Assert.Equal(32, reset.Hp);

        // The respawn is not re-sent on subsequent ticks.
        engine.Tick();
        Assert.Single(_sender.Sent, c => c.Kind == ControlKind.Reset);
    }

    [Fact]
    public void WaveMode_RespawnsAllDeadOnTheInterval()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(5), waveInterval: TimeSpan.FromSeconds(30));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2), Msg.Hb("c", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0));
        engine.OnMessage(Msg.Hit("b", 1, 2, hpAfter: 0));

        _clock.Advance(TimeSpan.FromSeconds(30));
        engine.Tick();

        List<Control> resets = _sender.Sent.Where(c => c.Kind == ControlKind.Reset).ToList();
        Assert.Equal(2, resets.Count);
        Assert.Equal(new[] { "a", "b" }, resets.Select(r => r.Id).Order().ToArray());
    }

    [Fact]
    public void TimerExpiry_FinishesWithHighestScoringTeam()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(1));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 30));

        _clock.Advance(TimeSpan.FromMinutes(1));
        engine.Tick();

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(2, engine.Snapshot().Winner);
    }

    [Fact]
    public void TimerExpiry_WithTiedScores_IsADraw()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(1));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2));

        _clock.Advance(TimeSpan.FromMinutes(1));
        engine.Tick();

        Assert.Equal(0, engine.Snapshot().Winner);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `dotnet test dotnet/LaserTag.sln --filter "FullyQualifiedName~DeathmatchModeTests" 2>&1 | tail -10`
Expected: compile errors (`DeathmatchMode` missing).

- [ ] **Step 3: Implement `DeathmatchMode.cs`**

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// Timed team deathmatch: +<c>hitPoints</c> per hit for the shooter's team,
/// +<c>killPoints</c> more when the shot kills; dead players respawn either
/// per-player after a delay or in synced waves; highest team score when the
/// timer expires wins (tie → draw).
/// </summary>
public sealed class DeathmatchMode : IGameMode
{
    private readonly int _hitPoints;
    private readonly int _killPoints;
    private readonly TimeSpan? _respawnDelay;
    private readonly TimeSpan? _waveInterval;
    private readonly Dictionary<string, DateTimeOffset> _pendingRespawns = new(StringComparer.Ordinal);
    private DateTimeOffset _nextWaveAt;

    /// <summary>Initializes the mode.</summary>
    /// <param name="duration">Fixed match length.</param>
    /// <param name="hitPoints">Points per hit. Defaults to 1.</param>
    /// <param name="killPoints">Extra points for a killing hit. Defaults to 5.</param>
    /// <param name="respawnDelay">
    /// Per-player respawn delay. Defaults to 10 s. Ignored when
    /// <paramref name="waveInterval"/> is set.
    /// </param>
    /// <param name="waveInterval">
    /// When set, all dead players respawn together every interval instead of
    /// per-player delays.
    /// </param>
    public DeathmatchMode(
        TimeSpan duration,
        int hitPoints = 1,
        int killPoints = 5,
        TimeSpan? respawnDelay = null,
        TimeSpan? waveInterval = null)
    {
        MatchDuration = duration;
        _hitPoints = hitPoints;
        _killPoints = killPoints;
        _waveInterval = waveInterval;
        _respawnDelay = waveInterval is null ? respawnDelay ?? TimeSpan.FromSeconds(10) : null;
    }

    /// <inheritdoc/>
    public string Name => "deathmatch";

    /// <inheritdoc/>
    public TimeSpan? MatchDuration { get; }

    /// <inheritdoc/>
    public void OnMatchStart(MatchContext context)
    {
        _pendingRespawns.Clear();
        if (_waveInterval is { } wave)
        {
            _nextWaveAt = context.Now + wave;
        }
    }

    /// <inheritdoc/>
    public void OnHit(MatchContext context, HitEvent hit)
    {
        bool killed = hit.Hp <= 0;
        context.AddScore(hit.ShooterTeam, _hitPoints + (killed ? _killPoints : 0));
        if (killed && _respawnDelay is { } delay)
        {
            _pendingRespawns[hit.Victim] = context.Now + delay;
        }
    }

    /// <inheritdoc/>
    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant)
    {
        // A device that respawned by other means (manual reset) needs no pending respawn.
        if (participant.Alive)
        {
            _pendingRespawns.Remove(participant.Id);
        }
    }

    /// <inheritdoc/>
    public void OnTick(MatchContext context)
    {
        if (_respawnDelay is not null)
        {
            foreach ((string id, DateTimeOffset due) in _pendingRespawns.ToList())
            {
                if (context.Now >= due)
                {
                    context.Send(new Control { Kind = ControlKind.Reset, Hp = context.StartHp, Id = id });
                    _pendingRespawns.Remove(id);
                }
            }
        }
        else if (_waveInterval is { } wave && context.Now >= _nextWaveAt)
        {
            // Per-id resets (not a bare broadcast): a broadcast reset would
            // also heal alive players, which changes the game.
            foreach (Participant p in context.Participants.Where(p => !p.Alive))
            {
                context.Send(new Control { Kind = ControlKind.Reset, Hp = context.StartHp, Id = p.Id });
            }

            _nextWaveAt += wave;
        }
    }

    /// <inheritdoc/>
    public MatchResult? CheckEnd(MatchContext context)
    {
        if (context.Now - context.MatchStartedAt < MatchDuration)
        {
            return null;
        }

        if (context.Scores.Count == 0)
        {
            return new MatchResult(0);
        }

        int best = context.Scores.Values.Max();
        List<int> leaders = context.Scores.Where(kv => kv.Value == best).Select(kv => kv.Key).ToList();
        return new MatchResult(leaders.Count == 1 ? leaders[0] : 0);
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `dotnet test dotnet/LaserTag.sln 2>&1 | tail -5`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add dotnet/LaserTag.Game dotnet/LaserTag.Game.Tests
git commit -m "Add DeathmatchMode: team scoring, per-player and wave respawns, timed winner"
```

---

### Task 7: EliminationMode

**Files:**
- Create: `dotnet/LaserTag.Game/EliminationMode.cs`
- Test: `dotnet/LaserTag.Game.Tests/EliminationModeTests.cs`

**Interfaces:**
- Consumes: same contracts as Task 6.
- Produces: `EliminationMode(TimeSpan? timerCap = null)`; `Name == "elimination"`; no respawns; ends when ≤1 team has an alive **online** participant (that team wins; none → draw); with `timerCap`, expiry → team with most alive players wins, tie → draw.

- [ ] **Step 1: Write the failing tests**

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

public class EliminationModeTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();

    private MatchEngine Running(EliminationMode mode, params Heartbeat[] lobby)
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);
        engine.StartMatch(mode, lobby);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();
        _sender.Sent.Clear();
        return engine;
    }

    [Fact]
    public void LastTeamStanding_Wins()
    {
        MatchEngine engine = Running(new EliminationMode(), Msg.Hb("a", 1), Msg.Hb("b", 2), Msg.Hb("c", 2));

        engine.OnMessage(Msg.Hit("b", 1, 2, hpAfter: 0));
        Assert.Equal(MatchPhase.Running, engine.Phase); // c still alive on team 2

        engine.OnMessage(Msg.Hit("c", 1, 2, hpAfter: 0));

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(1, engine.Snapshot().Winner);
    }

    [Fact]
    public void NoRespawns_NoResetEverSent()
    {
        MatchEngine engine = Running(new EliminationMode(), Msg.Hb("a", 1), Msg.Hb("b", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0));

        _clock.Advance(TimeSpan.FromMinutes(2));
        engine.Tick();

        Assert.DoesNotContain(_sender.Sent, c => c.Kind == ControlKind.Reset);
    }

    [Fact]
    public void OfflineDevice_DoesNotCountAsAlive()
    {
        MatchEngine engine = Running(new EliminationMode(), Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.MarkOffline("b");
        engine.Tick(); // team 2 has no alive+online member left

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(1, engine.Snapshot().Winner);
    }

    [Fact]
    public void TimerCap_MostAlivePlayersWins()
    {
        var mode = new EliminationMode(timerCap: TimeSpan.FromMinutes(10));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 1), Msg.Hb("c", 2), Msg.Hb("d", 2));
        engine.OnMessage(Msg.Hit("c", 1, 2, hpAfter: 0));

        _clock.Advance(TimeSpan.FromMinutes(10));
        engine.Tick();

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(1, engine.Snapshot().Winner); // team 1: 2 alive vs team 2: 1
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `dotnet test dotnet/LaserTag.sln --filter "FullyQualifiedName~EliminationModeTests" 2>&1 | tail -10`
Expected: compile errors.

- [ ] **Step 3: Implement `EliminationMode.cs`**

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// Elimination: no respawns, death is permanent for the round; the last team
/// with an alive online participant wins. An optional timer cap ends the round
/// with the most-alive team winning (tie → draw).
/// </summary>
public sealed class EliminationMode : IGameMode
{
    /// <summary>Initializes the mode.</summary>
    /// <param name="timerCap">
    /// Optional safety cap; when it expires the team with the most alive
    /// players wins. <see langword="null"/> = play until one team stands.
    /// </param>
    public EliminationMode(TimeSpan? timerCap = null) => MatchDuration = timerCap;

    /// <inheritdoc/>
    public string Name => "elimination";

    /// <inheritdoc/>
    public TimeSpan? MatchDuration { get; }

    /// <inheritdoc/>
    public void OnMatchStart(MatchContext context)
    {
    }

    /// <inheritdoc/>
    public void OnHit(MatchContext context, HitEvent hit)
    {
    }

    /// <inheritdoc/>
    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant)
    {
    }

    /// <inheritdoc/>
    public void OnTick(MatchContext context)
    {
    }

    /// <inheritdoc/>
    public MatchResult? CheckEnd(MatchContext context)
    {
        Dictionary<int, int> aliveByTeam = context.Participants
            .Where(p => p.Alive && p.Online)
            .GroupBy(p => p.Team)
            .ToDictionary(g => g.Key, g => g.Count());

        if (aliveByTeam.Count <= 1)
        {
            return new MatchResult(aliveByTeam.Count == 1 ? aliveByTeam.Keys.First() : 0);
        }

        if (MatchDuration is { } cap && context.Now - context.MatchStartedAt >= cap)
        {
            int best = aliveByTeam.Values.Max();
            List<int> leaders = aliveByTeam.Where(kv => kv.Value == best).Select(kv => kv.Key).ToList();
            return new MatchResult(leaders.Count == 1 ? leaders[0] : 0);
        }

        return null;
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `dotnet test dotnet/LaserTag.sln 2>&1 | tail -5`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add dotnet/LaserTag.Game dotnet/LaserTag.Game.Tests
git commit -m "Add EliminationMode: last team standing, offline exclusion, timer cap"
```

---

### Task 8: DurationParser (command-arg parsing, testable)

**Files:**
- Create: `dotnet/LaserTag.Game/DurationParser.cs`
- Test: `dotnet/LaserTag.Game.Tests/DurationParserTests.cs`

**Interfaces:**
- Produces: `static bool DurationParser.TryParse(string? text, out TimeSpan value)` — accepts `"5m"`, `"90s"`, `"1h"`, `"300"` (bare number = seconds); rejects null/empty/zero/negative/garbage.

- [ ] **Step 1: Write the failing tests**

```csharp
namespace LaserTag.Game.Tests;

public class DurationParserTests
{
    [Theory]
    [InlineData("5m", 300)]
    [InlineData("90s", 90)]
    [InlineData("1h", 3600)]
    [InlineData("300", 300)]
    public void TryParse_ValidInputs(string text, int expectedSeconds)
    {
        Assert.True(DurationParser.TryParse(text, out TimeSpan value));
        Assert.Equal(TimeSpan.FromSeconds(expectedSeconds), value);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("0m")]
    [InlineData("-5m")]
    [InlineData("5x")]
    [InlineData("m")]
    [InlineData("five")]
    public void TryParse_InvalidInputs_ReturnFalse(string? text)
    {
        Assert.False(DurationParser.TryParse(text, out _));
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `dotnet test dotnet/LaserTag.sln --filter "FullyQualifiedName~DurationParserTests" 2>&1 | tail -10`
Expected: compile errors.

- [ ] **Step 3: Implement `DurationParser.cs`**

```csharp
using System.Globalization;

namespace LaserTag.Game;

/// <summary>
/// Parses human-friendly console durations: <c>5m</c>, <c>90s</c>, <c>1h</c>,
/// or a bare number of seconds.
/// </summary>
public static class DurationParser
{
    /// <summary>Attempts to parse a duration token.</summary>
    /// <param name="text">The token, e.g. <c>5m</c>.</param>
    /// <param name="value">The parsed positive duration on success.</param>
    /// <returns><see langword="true"/> on success.</returns>
    public static bool TryParse(string? text, out TimeSpan value)
    {
        value = TimeSpan.Zero;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        string trimmed = text.Trim();
        double multiplier = 1;
        char last = char.ToLowerInvariant(trimmed[^1]);
        string digits = trimmed;
        if (last is 's' or 'm' or 'h')
        {
            multiplier = last switch { 'm' => 60, 'h' => 3600, _ => 1 };
            digits = trimmed[..^1];
        }

        if (!double.TryParse(digits, NumberStyles.Float, CultureInfo.InvariantCulture, out double amount) ||
            amount <= 0)
        {
            return false;
        }

        value = TimeSpan.FromSeconds(amount * multiplier);
        return true;
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `dotnet test dotnet/LaserTag.sln 2>&1 | tail -5`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add dotnet/LaserTag.Game dotnet/LaserTag.Game.Tests
git commit -m "Add DurationParser for console match-length arguments"
```

---

### Task 9: LaserTag.Host — Generic Host app (telemetry service, engine service, console REPL)

**Files:**
- Create: `dotnet/LaserTag.Host/LaserTag.Host.csproj`
- Create: `dotnet/LaserTag.Host/Program.cs`
- Create: `dotnet/LaserTag.Host/GameService.cs`
- Create: `dotnet/LaserTag.Host/UdpTelemetryService.cs`
- Create: `dotnet/LaserTag.Host/MatchEngineService.cs`
- Create: `dotnet/LaserTag.Host/ConsoleUiService.cs`
- Modify: `dotnet/LaserTag.sln`

**Interfaces:**
- Consumes: everything above (`MatchEngine`, modes, `DurationParser`, `UdpControlSender`, `BroadcastAddress`, `UdpMessageParser`, `DeviceRoster`).
- Produces: a runnable `dotnet run --project dotnet/LaserTag.Host [-- --broadcast 192.168.1.255]`. This task has no unit tests (thin IO shell — all logic already tested); verification is build + manual smoke.

- [ ] **Step 1: Create the project**

```bash
cd dotnet
dotnet new console -n LaserTag.Host -f net10.0
dotnet add LaserTag.Host reference LaserTag.Game LaserTag.Client
dotnet add LaserTag.Host package Microsoft.Extensions.Hosting
dotnet add LaserTag.Host package Spectre.Console
dotnet sln add LaserTag.Host
```

Match the `<PropertyGroup>` conventions (`GenerateDocumentationFile` true; console template already has ImplicitUsings/Nullable).

- [ ] **Step 2: Implement `GameService.cs`** (thread-safe facade over the engine — the one lock in the app)

```csharp
using LaserTag.Client;
using LaserTag.Client.Models;
using LaserTag.Game;

namespace LaserTag.Host;

/// <summary>
/// Thread-safe facade over the single <see cref="MatchEngine"/> instance: the
/// telemetry loop, tick loop, and console REPL all funnel through this lock.
/// </summary>
public sealed class GameService
{
    private readonly object _gate = new();
    private readonly MatchEngine _engine;
    private readonly DeviceRoster _roster;
    private readonly IControlSender _sender;
    private readonly Dictionary<string, bool> _lastOnline = new(StringComparer.Ordinal);

    /// <summary>Raised with a printable line whenever something noteworthy happens.</summary>
    public event Action<string>? Event;

    /// <summary>Initializes the service.</summary>
    /// <param name="sender">The CTL transport (shared with the engine).</param>
    public GameService(IControlSender sender)
    {
        _sender = sender;
        _engine = new MatchEngine(sender, () => DateTimeOffset.UtcNow);
        _roster = new DeviceRoster(() => DateTimeOffset.UtcNow);
    }

    /// <summary>Feeds a parsed telemetry message to the roster + engine.</summary>
    /// <param name="message">The parsed message.</param>
    public void OnMessage(UdpInboundMessage message)
    {
        lock (_gate)
        {
            if (message is Heartbeat hb)
            {
                _roster.Ingest(hb);
                _lastOnline[hb.Id] = true;
            }

            _engine.OnMessage(message);
        }

        if (message is HitEvent hit)
        {
            Event?.Invoke($"HIT {hit.Victim} by team {hit.ShooterTeam} dmg={hit.Dmg} hp={hit.Hp}");
        }
        else if (message is StateEvent st)
        {
            Event?.Invoke($"STATE {st.Source} -> {st.S}{(st.Hp is { } hp ? $" hp={hp}" : string.Empty)}");
        }
    }

    /// <summary>Advances the engine clock and propagates roster liveness.</summary>
    public void Tick()
    {
        MatchPhase before, after;
        int? winner = null;
        lock (_gate)
        {
            foreach (RosterEntry entry in _roster.Entries())
            {
                bool wasOnline = _lastOnline.GetValueOrDefault(entry.Id, entry.Online);
                if (wasOnline && !entry.Online)
                {
                    _engine.MarkOffline(entry.Id);
                    Event?.Invoke($"OFFLINE {entry.Id}");
                }

                _lastOnline[entry.Id] = entry.Online;
            }

            before = _engine.Phase;
            _engine.Tick();
            after = _engine.Phase;
            if (after == MatchPhase.Finished)
            {
                winner = _engine.Snapshot().Winner;
            }
        }

        if (before != after)
        {
            Event?.Invoke(after == MatchPhase.Finished
                ? $"GAME OVER — winner: {(winner == 0 ? "draw" : $"team {winner}")}"
                : $"PHASE {before} -> {after}");
        }
    }

    /// <summary>Starts a match with the currently online roster as the lobby.</summary>
    /// <param name="mode">The game mode.</param>
    /// <returns>An error string, or <see langword="null"/> on success.</returns>
    public string? StartMatch(IGameMode mode)
    {
        lock (_gate)
        {
            List<Heartbeat> lobby = _roster.Entries()
                .Where(e => e.Online)
                .Select(e => e.LastHeartbeat)
                .ToList();
            if (lobby.Count == 0)
            {
                return "No online devices — nothing to start.";
            }

            try
            {
                _engine.StartMatch(mode, lobby);
                return null;
            }
            catch (InvalidOperationException ex)
            {
                return ex.Message;
            }
        }
    }

    /// <summary>Stops the current match.</summary>
    public void Stop()
    {
        lock (_gate)
        {
            _engine.Stop();
        }
    }

    /// <summary>Takes a display snapshot.</summary>
    /// <returns>The snapshot.</returns>
    public MatchSnapshot Snapshot()
    {
        lock (_gate)
        {
            return _engine.Snapshot();
        }
    }

    /// <summary>Lists the current roster entries.</summary>
    /// <returns>The entries.</returns>
    public IReadOnlyList<RosterEntry> Devices()
    {
        lock (_gate)
        {
            return _roster.Entries().ToList();
        }
    }

    /// <summary>Sends an ad-hoc control message (reset/activate/deactivate verbs).</summary>
    /// <param name="control">The control to send.</param>
    public void SendControl(Control control) => _ = _sender.SendAsync(control);
}
```

> If `DeviceRoster` exposes a different enumeration member than `Entries()`, check `DeviceRoster.cs` and use the actual member (e.g. `All()`/`Snapshot()`); adjust `RosterEntry` usage accordingly — do NOT add a duplicate roster.

- [ ] **Step 3: Implement the two background services**

`UdpTelemetryService.cs`:

```csharp
using System.Net;
using System.Net.Sockets;
using System.Text;
using LaserTag.Client;
using LaserTag.Client.Models;
using Microsoft.Extensions.Hosting;

namespace LaserTag.Host;

/// <summary>Listens on UDP 4210 and feeds parsed telemetry to <see cref="GameService"/>.</summary>
public sealed class UdpTelemetryService(GameService game) : BackgroundService
{
    /// <summary>The devices' telemetry/CTL port.</summary>
    public const int Port = 4210;

    private readonly UdpMessageParser _parser = new();

    /// <inheritdoc/>
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var client = new UdpClient();
        client.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        client.Client.Bind(new IPEndPoint(IPAddress.Any, Port));

        while (!stoppingToken.IsCancellationRequested)
        {
            UdpReceiveResult result;
            try
            {
                result = await client.ReceiveAsync(stoppingToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (SocketException)
            {
                continue; // transient (e.g. ICMP port-unreachable reflections on Windows)
            }

            string line = Encoding.ASCII.GetString(result.Buffer);
            if (_parser.Parse(line) is { } message)
            {
                game.OnMessage(message);
            }
        }
    }
}
```

`MatchEngineService.cs`:

```csharp
using Microsoft.Extensions.Hosting;

namespace LaserTag.Host;

/// <summary>Drives <see cref="GameService.Tick"/> every 250 ms.</summary>
public sealed class MatchEngineService(GameService game) : BackgroundService
{
    /// <inheritdoc/>
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(250));
        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken).ConfigureAwait(false))
            {
                game.Tick();
            }
        }
        catch (OperationCanceledException)
        {
        }
    }
}
```

- [ ] **Step 4: Implement `ConsoleUiService.cs` + `Program.cs`**

`ConsoleUiService.cs`:

```csharp
using LaserTag.Client.Models;
using LaserTag.Game;
using Microsoft.Extensions.Hosting;
using Spectre.Console;

namespace LaserTag.Host;

/// <summary>
/// The interactive REPL: reads commands from stdin, prints the event feed and
/// scoreboard via Spectre.Console.
/// </summary>
public sealed class ConsoleUiService(GameService game, IHostApplicationLifetime lifetime) : BackgroundService
{
    /// <inheritdoc/>
    protected override Task ExecuteAsync(CancellationToken stoppingToken)
    {
        game.Event += line => AnsiConsole.MarkupLineInterpolated($"[grey]{DateTime.Now:HH:mm:ss}[/] {line}");
        return Task.Run(() => Repl(stoppingToken), stoppingToken);
    }

    private void Repl(CancellationToken stoppingToken)
    {
        AnsiConsole.MarkupLine("[bold]LaserTag host[/] — commands: devices, start dm <dur> [[--kill N]] [[--hit N]] [[--waves <dur>]], start elim [[--timer <dur>]], score, stop, reset [[id]], activate [[id]], deactivate [[id]], quit");
        while (!stoppingToken.IsCancellationRequested)
        {
            string? line = Console.ReadLine();
            if (line is null)
            {
                break;
            }

            try
            {
                if (!Dispatch(line.Trim()))
                {
                    break; // quit
                }
            }
            catch (Exception ex)
            {
                AnsiConsole.MarkupLineInterpolated($"[red]error:[/] {ex.Message}");
            }
        }

        lifetime.StopApplication();
    }

    private bool Dispatch(string line)
    {
        string[] args = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (args.Length == 0)
        {
            return true;
        }

        switch (args[0].ToLowerInvariant())
        {
            case "quit" or "exit":
                return false;
            case "devices":
                PrintDevices();
                break;
            case "score":
                PrintScore();
                break;
            case "stop":
                game.Stop();
                break;
            case "start":
                StartMatch(args);
                break;
            case "reset":
                game.SendControl(new Control { Kind = ControlKind.Reset, Id = args.ElementAtOrDefault(1) });
                break;
            case "activate":
                game.SendControl(new Control { Kind = ControlKind.Activate, Id = args.ElementAtOrDefault(1) });
                break;
            case "deactivate":
                game.SendControl(new Control { Kind = ControlKind.Deactivate, Id = args.ElementAtOrDefault(1) });
                break;
            default:
                AnsiConsole.MarkupLine("[yellow]unknown command[/]");
                break;
        }

        return true;
    }

    private void StartMatch(string[] args)
    {
        string? kind = args.ElementAtOrDefault(1)?.ToLowerInvariant();
        IGameMode mode;
        if (kind == "dm")
        {
            if (!DurationParser.TryParse(args.ElementAtOrDefault(2), out TimeSpan duration))
            {
                AnsiConsole.MarkupLine("[yellow]usage: start dm <duration e.g. 5m>[/]");
                return;
            }

            int hit = IntOption(args, "--hit") ?? 1;
            int kill = IntOption(args, "--kill") ?? 5;
            TimeSpan? waves = DurationOption(args, "--waves");
            mode = new DeathmatchMode(duration, hit, kill, waveInterval: waves);
        }
        else if (kind == "elim")
        {
            mode = new EliminationMode(DurationOption(args, "--timer"));
        }
        else
        {
            AnsiConsole.MarkupLine("[yellow]usage: start dm <dur> | start elim[/]");
            return;
        }

        string? error = game.StartMatch(mode);
        AnsiConsole.MarkupLine(error is null
            ? $"[green]{mode.Name} starting — countdown![/]"
            : $"[red]{error}[/]");
    }

    private static int? IntOption(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length && int.TryParse(args[i + 1], out int v) ? v : null;
    }

    private static TimeSpan? DurationOption(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length && DurationParser.TryParse(args[i + 1], out TimeSpan v) ? v : null;
    }

    private void PrintDevices()
    {
        var table = new Table().AddColumns("id", "host", "ip", "team", "hp", "online");
        foreach (var e in game.Devices())
        {
            Heartbeat hb = e.LastHeartbeat;
            table.AddRow(e.Id, hb.Source, hb.Ip, hb.Team.ToString(), hb.Hp.ToString(), e.Online ? "yes" : "[red]no[/]");
        }

        AnsiConsole.Write(table);
    }

    private void PrintScore()
    {
        MatchSnapshot s = game.Snapshot();
        AnsiConsole.MarkupLineInterpolated(
            $"[bold]{s.ModeName}[/] phase={s.Phase} elapsed={s.Elapsed:mm\\:ss}{(s.Remaining is { } r ? $" remaining={r:mm\\:ss}" : string.Empty)}{(s.Winner is { } w ? $" winner={(w == 0 ? "draw" : $"team {w}")}" : string.Empty)}");
        var scores = new Table().AddColumns("team", "score");
        foreach ((int team, int pts) in s.TeamScores.OrderByDescending(kv => kv.Value))
        {
            scores.AddRow($"team {team}", pts.ToString());
        }

        AnsiConsole.Write(scores);
        var players = new Table().AddColumns("id", "host", "team", "hp", "alive", "online");
        foreach (Participant p in s.Participants.OrderBy(p => p.Team))
        {
            players.AddRow(p.Id, p.Hostname, p.Team.ToString(), p.Hp.ToString(), p.Alive ? "yes" : "[red]dead[/]", p.Online ? "yes" : "[red]no[/]");
        }

        AnsiConsole.Write(players);
    }
}
```

`Program.cs`:

```csharp
using System.Net;
using LaserTag.Client;
using LaserTag.Host;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Spectre.Console;

// --broadcast <ip> overrides NIC discovery (e.g. multiple adapters).
IPEndPoint? broadcast = null;
int flag = Array.IndexOf(args, "--broadcast");
if (flag >= 0 && flag + 1 < args.Length)
{
    broadcast = new IPEndPoint(IPAddress.Parse(args[flag + 1]), UdpTelemetryService.Port);
}

broadcast ??= BroadcastAddress.DiscoverLocalBroadcastEndpoint(UdpTelemetryService.Port);
if (broadcast is null)
{
    AnsiConsole.MarkupLine("[red]No usable IPv4 NIC found — pass --broadcast <subnet-broadcast-ip>.[/]");
    return 1;
}

AnsiConsole.MarkupLineInterpolated($"CTL broadcast target: [bold]{broadcast}[/]");

HostApplicationBuilder builder = Host.CreateApplicationBuilder(args);
builder.Logging.ClearProviders(); // keep the console clean for the REPL
builder.Services.AddSingleton<IControlSender>(new UdpControlSender(broadcast));
builder.Services.AddSingleton<GameService>();
builder.Services.AddHostedService<UdpTelemetryService>();
builder.Services.AddHostedService<MatchEngineService>();
builder.Services.AddHostedService<ConsoleUiService>();

await builder.Build().RunAsync();
return 0;
```

- [ ] **Step 5: Build, full test run, verify startup**

```bash
dotnet build dotnet/LaserTag.sln
dotnet test dotnet/LaserTag.sln 2>&1 | tail -5
```

Expected: build clean (docs warnings = failures to fix, `GenerateDocumentationFile` is on), all tests pass. Then a startup smoke (from repo root):

```bash
echo "quit" | dotnet run --project dotnet/LaserTag.Host
```

Expected: prints `CTL broadcast target: 192.168.1.255:4210` (or NIC-appropriate) and the command banner, then exits cleanly.

- [ ] **Step 6: Commit**

```bash
git add dotnet/LaserTag.Host dotnet/LaserTag.sln
git commit -m "Add LaserTag.Host: Generic Host console with telemetry listener, tick loop, REPL"
```

---

### Task 10: Docs + full verification

**Files:**
- Modify: `README.md` (Control plane section: add a short "Game manager (host)" subsection)
- Modify: `.docs/handoff.md` (mark Next Steps #7 delivered by Spec A; add Spec B/C pointers)

**Interfaces:** none — documentation.

- [ ] **Step 1: README** — under the Control plane section, add:

```markdown
### Game manager (host)

`dotnet/LaserTag.Host` orchestrates matches over the control plane (spec:
`docs/superpowers/specs/2026-07-12-game-manager-design.md`):

```sh
dotnet run --project dotnet/LaserTag.Host            # auto-detects the subnet broadcast
# devices | start dm 5m [--kill 5 --hit 1 --waves 30s] | start elim [--timer 10m]
# score | stop | reset [id] | activate [id] | deactivate [id] | quit
```

Match rules live in `dotnet/LaserTag.Game` (`IGameMode`: Deathmatch,
Elimination). Scoring is per-team — the IR protocol carries the shooter's
team, not a player id. CTL grammar v2 (`countdown`, `gameover`,
`activate`/`deactivate`, optional `id=` addressing) is emitted by the host
today; firmware behaviours for the new verbs land in Spec B.
```

- [ ] **Step 2: Handoff** — update Next Steps #7 to ✅ DONE with a pointer to the spec/plan, and note remaining follow-ups (Spec B firmware pass, Spec C hunt/retaliation, Claude-skill wrapper).

- [ ] **Step 3: Full verification**

```bash
dotnet test dotnet/LaserTag.sln 2>&1 | tail -5
pio test -e native   # firmware contract tests must still pass untouched (48)
```

Expected: all .NET tests green; native suite still 48/48 (no firmware files were touched — this is a regression tripwire, not a formality).

- [ ] **Step 4: Commit**

```bash
git add README.md .docs/handoff.md
git commit -m "Document host game manager; mark host-CLI next-step delivered"
```

---

## Self-Review (completed)

- **Spec coverage:** grammar v2 (T1), sender + broadcast rule (T2), framework/state (T3), lifecycle + countdown/gameover (T4), reconciliation/dropout/rejoin (T5), DM scoring + both respawn policies + tie (T6), Elimination + timer cap + offline exclusion (T7), console commands incl. activate/deactivate (T8–9), docs + manual-bench pointer (T10). Deferred items match the spec's out-of-scope list.
- **Known deviations from spec wording (deliberate, documented in tasks):** wave respawns send per-id resets rather than one bare broadcast (a broadcast reset would heal alive players); "stale event" protection is phase-gating (ignore hits outside Running) rather than device-timestamp comparison (device `ts` is `millis()` uptime — not comparable to host wall-clock); the v1 UI is REPL + event feed + on-demand `score` table rather than a continuously re-rendering live layout (Spectre `Live` fights interactive `ReadLine`; revisit post-v1 if wanted).
- **Type consistency check:** `MatchContext` ctor args match Task 3 across Tasks 4–7; `Control.N/Winner/Id` names consistent T1→T9; `IGameMode` member set identical in T3 contract and T6/T7 implementations; `Msg.Hb/Hit` helper signatures consistent across T4–T7 tests.
- **Open verification point for the implementer (flagged, not guessed):** `DeviceRoster`'s enumeration member name in Task 9 — check `DeviceRoster.cs` before wiring `GameService.Devices()`.
