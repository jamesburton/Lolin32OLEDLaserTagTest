using LaserTag.Game;

namespace LaserTag.Ui;

/// <summary>
/// The match parameters a manager UI collects, before validation.
/// </summary>
/// <remarks>
/// Deliberately a flat bag of nullable values: it mirrors a form (and a JSON
/// request body) rather than a constructed mode, so the same shape serves the
/// web form, the Android form and the HTTP API. Turning it into a mode is
/// <see cref="ModeFactory"/>'s job.
/// </remarks>
public sealed class MatchRequest
{
    /// <summary>Mode id: <c>dm</c>, <c>elim</c> or <c>chase</c>.</summary>
    public string Mode { get; set; } = "dm";

    /// <summary>Match duration (e.g. <c>5m</c>). Required for dm.</summary>
    public string? Duration { get; set; } = "5m";

    /// <summary>Points per hit (deathmatch).</summary>
    public int? HitPoints { get; set; }

    /// <summary>Points per kill (deathmatch).</summary>
    public int? KillPoints { get; set; }

    /// <summary>Respawn wave interval (deathmatch); null means no waves.</summary>
    public string? WaveInterval { get; set; }

    /// <summary>Optional time limit (elimination).</summary>
    public string? Timer { get; set; }

    /// <summary>First team to this many points wins (chase).</summary>
    public int? FirstTo { get; set; }

    /// <summary>Shortest active-target window (chase).</summary>
    public string? MinWindow { get; set; }

    /// <summary>Longest active-target window (chase).</summary>
    public string? MaxWindow { get; set; }

    /// <summary>Gap between targets (chase).</summary>
    public string? Gap { get; set; }

    /// <summary>Points deducted for hitting a dormant board (chase).</summary>
    public int? Penalty { get; set; }

    /// <summary>When true, dormant boards go dark instead of showing the score.</summary>
    public bool Dark { get; set; }
}
