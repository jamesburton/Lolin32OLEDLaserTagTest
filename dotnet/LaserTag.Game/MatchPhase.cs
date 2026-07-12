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
