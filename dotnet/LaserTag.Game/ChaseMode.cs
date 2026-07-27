using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// "Chase the target": one board at a time is activated with a randomized
/// self-timeout window; hitting it scores +1 for the shooter's team, an
/// optional penalty deducts for shooting a dormant board (floored at zero).
/// Ends on a fixed duration and/or a first-to-N score, whichever trips first.
/// The device enforces the window (self-timeout); this mode keeps a slack
/// fallback so a lost timeout EVT can never stall the match.
/// </summary>
public sealed class ChaseMode : IGameMode
{
    private static readonly TimeSpan Slack = TimeSpan.FromMilliseconds(1500);

    private readonly int? _firstTo;
    private readonly TimeSpan _minWindow;
    private readonly TimeSpan _maxWindow;
    private readonly TimeSpan _gap;
    private readonly int _penalty;
    private readonly string _display;
    private readonly Random _rng;

    private string? _activeId;
    private string? _previousId;
    private bool _inGap;
    private DateTimeOffset _phaseAt; // gap end, or active window + slack end

    /// <summary>Initializes the mode.</summary>
    /// <param name="duration">Fixed match length, or null for unlimited.</param>
    /// <param name="firstTo">Score that ends the match, or null for none.</param>
    /// <param name="minWindow">Minimum active window. Defaults to 2 s.</param>
    /// <param name="maxWindow">Maximum active window. Defaults to 5 s.</param>
    /// <param name="gap">Dark gap between rounds. Defaults to 1 s.</param>
    /// <param name="penalty">Points deducted for a dormant hit. 0 disables.</param>
    /// <param name="display">Dormant display: "score" or "dark".</param>
    /// <param name="rng">Injectable randomness (tests pass a seeded one).</param>
    /// <exception cref="ArgumentException">
    /// Thrown when neither <paramref name="duration"/> nor
    /// <paramref name="firstTo"/> is provided.
    /// </exception>
    public ChaseMode(
        TimeSpan? duration,
        int? firstTo,
        TimeSpan? minWindow = null,
        TimeSpan? maxWindow = null,
        TimeSpan? gap = null,
        int penalty = 0,
        string display = "score",
        Random? rng = null)
    {
        if (duration is null && firstTo is null)
        {
            throw new ArgumentException("chase needs a duration and/or a first-to target");
        }

        MatchDuration = duration;
        _firstTo = firstTo;
        _minWindow = minWindow ?? TimeSpan.FromSeconds(2);
        _maxWindow = maxWindow ?? TimeSpan.FromSeconds(5);
        _gap = gap ?? TimeSpan.FromSeconds(1);
        _penalty = penalty;
        _display = display;
        _rng = rng ?? new Random();
    }

    /// <inheritdoc/>
    public string Name => "chase";

    /// <inheritdoc/>
    public TimeSpan? MatchDuration { get; }

    /// <inheritdoc/>
    public void OnMatchStart(MatchContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        context.Send(new Control
        {
            Kind = ControlKind.ChaseOn,
            Penalty = _penalty > 0 ? 1 : 0,
            Display = _display,
        });
        _activeId = null;
        _previousId = null;
        _inGap = true;
        _phaseAt = context.Now + _gap;
    }

    /// <inheritdoc/>
    public void OnHit(MatchContext context, HitEvent hit)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(hit);
        if (hit.Dormant)
        {
            if (_penalty > 0)
            {
                // Floor at zero: scores are displayed on 8x8 matrices and
                // persisted as points — negatives have no representation.
                int deduct = Math.Min(_penalty, context.Scores.GetValueOrDefault(hit.ShooterTeam));
                if (deduct > 0)
                {
                    context.AddScore(hit.ShooterTeam, -deduct);
                }
            }

            return;
        }

        if (_activeId is null || hit.Victim != _activeId)
        {
            return; // stale or non-chase hit
        }

        context.AddScore(hit.ShooterTeam, 1);
        EndRound(context);
    }

    /// <inheritdoc/>
    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentNullException.ThrowIfNull(participant);
        if (state.S == "timeout" && participant.Id == _activeId)
        {
            EndRound(context);
        }
    }

    /// <inheritdoc/>
    public void OnTick(MatchContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        if (_activeId is { } id)
        {
            Participant? active = context.Participants.FirstOrDefault(p => p.Id == id);
            if (active is null || !active.Online || context.Now >= _phaseAt)
            {
                // Slack expiry (lost EVT) or the target vanished: defensively
                // deactivate on the wire and move on unscored.
                context.Send(new Control { Kind = ControlKind.Deactivate, Id = id });
                EndRound(context);
            }

            return;
        }

        if (_inGap && context.Now >= _phaseAt)
        {
            ActivateNext(context);
        }
    }

    /// <inheritdoc/>
    public MatchResult? CheckEnd(MatchContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        if (_firstTo is { } target && context.Scores.Count > 0 &&
            context.Scores.Values.Max() >= target)
        {
            return Leader(context);
        }

        if (MatchDuration is { } d && context.Now - context.MatchStartedAt >= d)
        {
            return Leader(context);
        }

        return null;
    }

    private static MatchResult Leader(MatchContext context)
    {
        if (context.Scores.Count == 0)
        {
            return new MatchResult(0);
        }

        int best = context.Scores.Values.Max();
        List<int> leaders = context.Scores.Where(kv => kv.Value == best).Select(kv => kv.Key).ToList();
        return new MatchResult(leaders.Count == 1 ? leaders[0] : 0);
    }

    private void EndRound(MatchContext context)
    {
        _previousId = _activeId;
        _activeId = null;
        _inGap = true;
        _phaseAt = context.Now + _gap;
    }

    private void ActivateNext(MatchContext context)
    {
        // Standalone scoreboard boards are wall displays, never targets.
        List<Participant> pool = context.Participants
            .Where(p => p.Online && p.Mode != "scoreboard")
            .ToList();
        if (pool.Count >= 3 && _previousId is { } prev)
        {
            // With 3+ boards, never repeat the previous target. With 2 the
            // exclusion would make the sequence a predictable alternation.
            pool.RemoveAll(p => p.Id == prev);
        }

        if (pool.Count == 0)
        {
            _phaseAt = context.Now + _gap; // nobody available; retry next gap
            return;
        }

        Participant target = pool[_rng.Next(pool.Count)];
        double windowMs = _minWindow.TotalMilliseconds +
            (_rng.NextDouble() * (_maxWindow - _minWindow).TotalMilliseconds);
        _activeId = target.Id;
        _inGap = false;
        _phaseAt = context.Now + TimeSpan.FromMilliseconds(windowMs) + Slack;
        context.Send(new Control
        {
            Kind = ControlKind.Activate,
            Id = target.Id,
            T = (int)windowMs,
        });
    }
}
