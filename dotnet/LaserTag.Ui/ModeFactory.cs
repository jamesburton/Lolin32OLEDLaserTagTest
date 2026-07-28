using LaserTag.Game;

namespace LaserTag.Ui;

/// <summary>
/// Turns a <see cref="MatchRequest"/> into a configured <see cref="IGameMode"/>.
/// </summary>
/// <remarks>
/// Shared by the web form, the Android form and the HTTP API so all three
/// accept exactly the same inputs and reject the same ones. This mirrors the
/// console REPL's argument parsing; the duration strings use the same
/// <see cref="DurationParser"/> grammar ("90s", "5m", "1h30m").
/// </remarks>
public static class ModeFactory
{
    /// <summary>Validates a request and builds its mode.</summary>
    /// <param name="request">The requested parameters.</param>
    /// <param name="mode">The built mode when this returns true.</param>
    /// <param name="error">A human-readable reason when this returns false.</param>
    /// <returns>True if the request was valid.</returns>
    public static bool TryCreate(MatchRequest request, out IGameMode mode, out string error)
    {
        mode = null!;
        error = string.Empty;
        switch (request.Mode?.ToLowerInvariant())
        {
            case "dm":
                if (!DurationParser.TryParse(request.Duration, out TimeSpan dmDuration))
                {
                    error = "Deathmatch needs a duration, e.g. 5m.";
                    return false;
                }

                mode = new DeathmatchMode(
                    dmDuration,
                    request.HitPoints ?? 1,
                    request.KillPoints ?? 5,
                    waveInterval: ParseOptional(request.WaveInterval));
                return true;

            case "elim":
                mode = new EliminationMode(ParseOptional(request.Timer));
                return true;

            case "chase":
                TimeSpan? chaseDuration = ParseOptional(request.Duration);
                if (chaseDuration is null && request.FirstTo is null)
                {
                    // Both end conditions are optional individually, but a mode
                    // with neither would never finish.
                    error = "Chase needs a duration, a first-to target, or both.";
                    return false;
                }

                mode = new ChaseMode(
                    chaseDuration,
                    request.FirstTo,
                    ParseOptional(request.MinWindow),
                    ParseOptional(request.MaxWindow),
                    ParseOptional(request.Gap),
                    request.Penalty ?? 0,
                    request.Dark ? "dark" : "score");
                return true;

            default:
                error = $"Unknown mode '{request.Mode}'. Use dm, elim or chase.";
                return false;
        }
    }

    /// <summary>Parses an optional duration, treating blank and invalid alike as absent.</summary>
    /// <param name="value">The raw text.</param>
    /// <returns>The duration, or null.</returns>
    private static TimeSpan? ParseOptional(string? value) =>
        DurationParser.TryParse(value, out TimeSpan parsed) ? parsed : null;
}
