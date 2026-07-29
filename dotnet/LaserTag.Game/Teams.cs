namespace LaserTag.Game;

/// <summary>
/// Team constants shared by the modes, the managers and the CLI. Mirrors the
/// firmware's <c>cp::TeamNone</c> / <c>cp::TeamColourCount</c>.
/// </summary>
public static class Teams
{
    /// <summary>
    /// The neutral team: a target on no side. Everyone may shoot it, hits on
    /// it score for the shooter's team, and it can never win a match.
    /// </summary>
    /// <remarks>
    /// This is a board's default. The firmware has never own-team filtered —
    /// every decoded shot damages the board that receives it — so neutrality
    /// is the honest description of an unassigned board rather than a new
    /// behaviour.
    /// </remarks>
    public const int None = 0;

    /// <summary>The lowest assignable team index.</summary>
    public const int Min = 1;

    /// <summary>The highest assignable team index (Vatos carries four).</summary>
    public const int Max = 4;

    /// <summary>Checks whether a value is assignable to a device.</summary>
    /// <param name="team">The candidate team, where 0 means neutral.</param>
    /// <returns><see langword="true"/> for 0..4.</returns>
    public static bool IsValid(int team) => team is >= None and <= Max;

    /// <summary>Renders a team for display.</summary>
    /// <param name="team">The team index, where 0 means neutral.</param>
    /// <returns><c>none</c> for the neutral team, otherwise the number.</returns>
    public static string Describe(int team) =>
        team == None ? "none" : team.ToString();

    /// <summary>
    /// Parses a team from user input, accepting <c>none</c>/<c>neutral</c>/
    /// <c>0</c> for the neutral team.
    /// </summary>
    /// <param name="text">The input text.</param>
    /// <param name="team">The parsed team when successful.</param>
    /// <returns><see langword="true"/> if the text named a valid team.</returns>
    public static bool TryParse(string? text, out int team)
    {
        team = None;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        string t = text.Trim();
        if (t.Equals("none", StringComparison.OrdinalIgnoreCase) ||
            t.Equals("neutral", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return int.TryParse(t, out team) && IsValid(team);
    }
}
