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
