namespace LaserTag.Game;

/// <summary>
/// A device participating in the current match, as derived from the telemetry
/// stream. Hp here mirrors the device-authoritative value; the engine never
/// pushes hp, it only observes.
/// </summary>
public sealed record Participant
{
    /// <summary>Gets the device id (stable, from heartbeats).</summary>
    public required string Id { get; init; }

    /// <summary>Gets the device hostname (e.g. <c>lasertag-matrix</c>).</summary>
    public required string Hostname { get; init; }

    /// <summary>
    /// Gets the team index the device reported at lobby time, or
    /// <see cref="Teams.None"/> for a neutral target.
    /// </summary>
    public required int Team { get; init; }

    /// <summary>
    /// Gets a value indicating whether this is a neutral target — a device on
    /// no side. Everyone may shoot it and hits on it score for the shooter,
    /// but it is never a candidate for winning.
    /// </summary>
    public bool IsNeutral => Team == Teams.None;

    /// <summary>Gets the last observed hp.</summary>
    public required int Hp { get; init; }

    /// <summary>Gets a value indicating whether the participant is alive (hp &gt; 0).</summary>
    public required bool Alive { get; init; }

    /// <summary>Gets a value indicating whether the device is currently online.</summary>
    public required bool Online { get; init; }

    /// <summary>
    /// Gets the device's last reported mode id (e.g. <c>idle</c>,
    /// <c>scoreboard</c>). Chase excludes <c>scoreboard</c> boards — dedicated
    /// wall displays — from its target pool.
    /// </summary>
    public string Mode { get; init; } = "";

    /// <summary>Gets the time of death, when dead; used by respawn policies.</summary>
    public DateTimeOffset? DiedAt { get; init; }
}
