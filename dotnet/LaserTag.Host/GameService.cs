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
    private readonly Dictionary<int, int> _pushedScores = [];
    private DateTimeOffset _lastScorePushAt = DateTimeOffset.MinValue;
    private bool _finalScoresPushed;

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
        List<string> offlineIds = [];
        MatchSnapshot snap;
        lock (_gate)
        {
            foreach (RosterEntry entry in _roster.GetAll())
            {
                bool wasOnline = _lastOnline.GetValueOrDefault(entry.Id, entry.Online);
                if (wasOnline && !entry.Online)
                {
                    _engine.MarkOffline(entry.Id);
                    offlineIds.Add(entry.Id);
                }

                _lastOnline[entry.Id] = entry.Online;
            }

            before = _engine.Phase;
            _engine.Tick();
            after = _engine.Phase;
            snap = _engine.Snapshot();
            if (after == MatchPhase.Finished)
            {
                winner = snap.Winner;
            }
        }

        // Console I/O (Event subscribers) must never run under _gate — it
        // would stall telemetry ingest on OnMessage, which also takes the
        // lock. Fire all events here, after release.
        foreach (string id in offlineIds)
        {
            Event?.Invoke($"OFFLINE {id}");
        }

        if (before != after)
        {
            Event?.Invoke(after == MatchPhase.Finished
                ? $"GAME OVER — winner: {(winner == 0 ? "draw" : $"team {winner}")}"
                : $"PHASE {before} -> {after}");
        }

        // Score display push (spec §2.1): on change + a 1 s refresh while a
        // match is live, plus one final push at Finished so gameover boards
        // hold the true final board. hp is never pushed — scores only.
        bool live = snap.Phase is MatchPhase.Running or MatchPhase.Countdown;
        bool changed = snap.TeamScores.Count != _pushedScores.Count ||
            snap.TeamScores.Any(kv => _pushedScores.GetValueOrDefault(kv.Key) != kv.Value);
        DateTimeOffset now = DateTimeOffset.UtcNow;
        if ((live && (changed || now - _lastScorePushAt >= TimeSpan.FromSeconds(1))) ||
            (snap.Phase == MatchPhase.Finished && changed && !_finalScoresPushed))
        {
            _pushedScores.Clear();
            foreach ((int team, int pts) in snap.TeamScores)
            {
                _pushedScores[team] = pts;
            }

            _lastScorePushAt = now;
            _finalScoresPushed = snap.Phase == MatchPhase.Finished;
            _ = _sender.SendAsync(new Control { Kind = ControlKind.Score, Scores = new Dictionary<int, int>(_pushedScores) });
        }

        if (live)
        {
            _finalScoresPushed = false;
        }
    }

    /// <summary>Starts a match with the currently online roster as the lobby.</summary>
    /// <param name="mode">The game mode.</param>
    /// <returns>An error string, or <see langword="null"/> on success.</returns>
    public string? StartMatch(IGameMode mode)
    {
        lock (_gate)
        {
            List<Heartbeat> lobby = _roster.GetAll()
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
            return _roster.GetAll().ToList();
        }
    }

    /// <summary>Sends an ad-hoc control message (reset/activate/deactivate verbs).</summary>
    /// <param name="control">The control to send.</param>
    public void SendControl(Control control) => _ = _sender.SendAsync(control);
}
