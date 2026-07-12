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
    private readonly Dictionary<string, DateTimeOffset> _handledDeaths = new(StringComparer.Ordinal);
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
        _handledDeaths.Clear();
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
    }

    /// <inheritdoc/>
    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant)
    {
    }

    /// <inheritdoc/>
    public void OnTick(MatchContext context)
    {
        if (_respawnDelay is { } delay)
        {
            // Derive respawns from Participant.DiedAt rather than a
            // hit-populated pending set: the engine stamps DiedAt on
            // hit-, state-, AND heartbeat-detected deaths, so this also
            // catches a death whose kill EVT was lost and only surfaced via
            // heartbeat reconciliation. Keyed on the exact DiedAt instant so
            // a later death (new DiedAt) is handled again without needing
            // stale-key cleanup.
            foreach (Participant p in context.Participants)
            {
                if (p.Alive || p.DiedAt is not { } diedAt)
                {
                    continue;
                }

                if (_handledDeaths.TryGetValue(p.Id, out DateTimeOffset handled) && handled == diedAt)
                {
                    continue;
                }

                if (context.Now >= diedAt + delay)
                {
                    context.Send(new Control { Kind = ControlKind.Reset, Hp = context.StartHp, Id = p.Id });
                    _handledDeaths[p.Id] = diedAt;
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
