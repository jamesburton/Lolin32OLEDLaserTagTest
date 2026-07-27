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
                Mode = hb.Mode,
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
    /// Stopping during Countdown (the match never actually ran) sends a plain
    /// <see cref="ControlKind.Stop"/> cue rather than a GameOver — there is no
    /// result to celebrate. Stopping during Running still sends GameOver.
    /// </summary>
    public void Stop()
    {
        if (Phase is not (MatchPhase.Countdown or MatchPhase.Running))
        {
            return;
        }

        if (Phase == MatchPhase.Countdown)
        {
            Phase = MatchPhase.Finished;
            _winner = 0;
            Send(new Control { Kind = ControlKind.Stop });
            return;
        }

        MatchResult? result = _mode!.CheckEnd(Context());
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

        // Pass the live dictionary directly (exposed as IReadOnlyDictionary on
        // MatchContext, so modes still can't mutate it): AddScore mutates
        // _scores via the closure below, and a same-event CheckEnd must see
        // that update immediately rather than a stale copy.
        scores: _scores,
        addScore: (team, pts) => _scores[team] = _scores.GetValueOrDefault(team) + pts,
        send: Send);

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
            bool died = hp <= 0 && participant.Alive;
            _participants[participant.Id] = participant with
            {
                Hp = hp,
                Alive = hp > 0,
                DiedAt = died ? _clock() : (hp > 0 ? null : participant.DiedAt),
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

        // A rejoining participant that was already dead before this heartbeat
        // (tracked offline while dead) and whose heartbeat still reports
        // hp<=0 must come back DEAD, not resurrected by a plain Start — a
        // WiFi blip must not undo a kill. Only a heartbeat reporting hp>0
        // (the device rebooted, hp is volatile and device-authoritative)
        // gets the reboot-recovery Start; that remains a documented
        // limitation (see spec post-impl notes).
        bool wasDeadBeforeReconciliation = !p.Alive;
        bool rejoinedDead = rejoined && wasDeadBeforeReconciliation && hb.Hp <= 0;

        // Reconcile the authoritative hp from the heartbeat: covers lost EVT
        // packets. Never scores (shooter unknown) — spec "unattributed hit".
        bool died = hb.Hp <= 0 && p.Alive;
        _participants[hb.Id] = p with
        {
            Mode = hb.Mode,
            Hp = hb.Hp,
            Alive = hb.Hp > 0,
            Online = true,
            DiedAt = died ? _clock() : (hb.Hp > 0 ? null : p.DiedAt),
        };

        if (rejoinedDead)
        {
            Send(new Control { Kind = ControlKind.Reset, Hp = 0, Id = hb.Id });
        }
        else if (rejoined)
        {
            Send(new Control { Kind = ControlKind.Start, Id = hb.Id });
        }

        if (Phase == MatchPhase.Running)
        {
            CheckEnd(Context());
        }
    }

    private void Send(Control control) =>

        // CTL is fire-and-forget (repeats handled by the sender); the engine
        // stays synchronous, so sends are intentionally not awaited.
        _ = _sender.SendAsync(control);
}
