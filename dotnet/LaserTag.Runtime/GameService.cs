using LaserTag.Client;
using LaserTag.Client.Models;
using LaserTag.Game;

namespace LaserTag.Runtime;

/// <summary>
/// Thread-safe facade over the single <see cref="MatchEngine"/> instance: the
/// telemetry loop, tick loop, and every UI shell (console REPL, web manager,
/// Android app) all funnel through this lock.
/// </summary>
public sealed class GameService
{
    /// <summary>How many recent event lines are retained for UI feeds.</summary>
    private const int EventHistoryLimit = 200;

    private readonly object _gate = new();
    private readonly MatchEngine _engine;
    private readonly DeviceRoster _roster;
    private readonly IControlSender _sender;
    private readonly Dictionary<string, bool> _lastOnline = new(StringComparer.Ordinal);
    private readonly Dictionary<int, int> _pushedScores = [];
    private readonly LinkedList<string> _recentEvents = new();
    private readonly TeamAssigner _assigner = new();
    private DateTimeOffset _lastScorePushAt = DateTimeOffset.MinValue;
    private bool _finalScoresPushed;

    /// <summary>Raised with a printable line whenever something noteworthy happens.</summary>
    public event Action<string>? Event;

    /// <summary>
    /// Raised after every tick so a UI can re-render on change.
    /// </summary>
    /// <remarks>
    /// Separate from <see cref="Event"/>, which carries printable lines for the
    /// console and the live feed. Graphical shells need a "something moved"
    /// signal rather than text, and polling a 4 Hz engine from a UI thread
    /// wastes work when most ticks change nothing visible.
    /// </remarks>
    public event Action? StateChanged;

    /// <summary>The most recent event lines, oldest first, for UI event feeds.</summary>
    /// <returns>A snapshot copy, safe to enumerate off-thread.</returns>
    public IReadOnlyList<string> RecentEvents()
    {
        lock (_gate)
        {
            return [.. _recentEvents];
        }
    }

    /// <summary>Records an event line and forwards it to subscribers.</summary>
    /// <param name="line">The printable line.</param>
    private void Raise(string line)
    {
        lock (_gate)
        {
            _recentEvents.AddLast(line);
            while (_recentEvents.Count > EventHistoryLimit)
            {
                _recentEvents.RemoveFirst();
            }
        }

        Event?.Invoke(line);
    }

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
            Raise($"HIT {hit.Victim} by team {hit.ShooterTeam} dmg={hit.Dmg} hp={hit.Hp}");
        }
        else if (message is StateEvent st)
        {
            Raise($"STATE {st.Source} -> {st.S}{(st.Hp is { } hp ? $" hp={hp}" : string.Empty)}");
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
            Raise($"OFFLINE {id}");
        }

        if (before != after)
        {
            Raise(after == MatchPhase.Finished
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

        // Fired outside the lock for the same reason as the Event raises above:
        // a UI re-render must never be able to stall telemetry ingest.
        StateChanged?.Invoke();
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

    /// <summary>
    /// Assigns one device's team by patching its persisted config over REST.
    /// </summary>
    /// <param name="deviceId">The device id from the roster.</param>
    /// <param name="team">The team (0 = neutral, 1..4 = a side).</param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>An error message, or <see langword="null"/> on success.</returns>
    /// <remarks>
    /// A change made mid-match does NOT move the player: the lobby snapshots
    /// each participant's team at start, so the new team applies from the next
    /// match. That is deliberate — reassigning sides mid-round would silently
    /// rewrite who the existing scores belonged to.
    /// </remarks>
    public async Task<string?> SetTeamAsync(
        string deviceId,
        int team,
        CancellationToken cancellationToken = default)
    {
        if (!Teams.IsValid(team))
        {
            return $"team must be 0-4 (0 = none), got {team}";
        }

        string? ip;
        lock (_gate)
        {
            ip = _roster.GetAll().FirstOrDefault(e => e.Id == deviceId)?.LastHeartbeat.Ip;
        }

        if (ip is null)
        {
            return $"no device with id {deviceId}";
        }

        TeamAssigner.Result result = await _assigner.AssignAsync(ip, team, cancellationToken)
            .ConfigureAwait(false);
        if (!result.Ok)
        {
            return result.Error;
        }

        // The roster's team mirror refreshes from the next heartbeat (~2 s);
        // nudge the UI now so the change does not look like it was ignored.
        StateChanged?.Invoke();
        return null;
    }
}
