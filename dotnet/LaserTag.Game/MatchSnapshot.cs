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
