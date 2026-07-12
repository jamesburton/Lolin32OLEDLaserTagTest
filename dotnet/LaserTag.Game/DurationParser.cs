using System.Globalization;

namespace LaserTag.Game;

/// <summary>
/// Parses human-friendly console durations: <c>5m</c>, <c>90s</c>, <c>1h</c>,
/// or a bare number of seconds.
/// </summary>
public static class DurationParser
{
    /// <summary>Attempts to parse a duration token.</summary>
    /// <param name="text">The token, e.g. <c>5m</c>.</param>
    /// <param name="value">The parsed positive duration on success.</param>
    /// <returns><see langword="true"/> on success.</returns>
    public static bool TryParse(string? text, out TimeSpan value)
    {
        value = TimeSpan.Zero;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        string trimmed = text.Trim();
        double multiplier = 1;
        char last = char.ToLowerInvariant(trimmed[^1]);
        string digits = trimmed;
        if (last is 's' or 'm' or 'h')
        {
            multiplier = last switch { 'm' => 60, 'h' => 3600, _ => 1 };
            digits = trimmed[..^1];
        }

        if (!double.TryParse(digits, NumberStyles.Float, CultureInfo.InvariantCulture, out double amount) ||
            amount <= 0)
        {
            return false;
        }

        value = TimeSpan.FromSeconds(amount * multiplier);
        return true;
    }
}
