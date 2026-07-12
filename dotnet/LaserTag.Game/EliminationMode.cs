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
