using System.Globalization;

namespace LaserTag.Rf;

/// <summary>
/// Parses the RF probe firmware's serial line protocol.
/// </summary>
public static class RfLineParser
{
    /// <summary>
    /// Attempts to parse one <c>RF …</c> capture line.
    /// </summary>
    /// <param name="line">A single line of probe output.</param>
    /// <param name="capture">The parsed capture when this returns true.</param>
    /// <returns>True if the line was a well-formed capture line.</returns>
    public static bool TryParse(string line, out RfCapture capture)
    {
        capture = default!;
        if (string.IsNullOrWhiteSpace(line) || !line.StartsWith("RF ", StringComparison.Ordinal))
        {
            return false;
        }

        int? channel = null;
        string? rate = null;
        long? ts = null;
        string? hex = null;
        foreach (string token in line.Split(' ', StringSplitOptions.RemoveEmptyEntries).Skip(1))
        {
            int eq = token.IndexOf('=');
            if (eq <= 0)
            {
                continue;
            }

            string key = token[..eq];
            string value = token[(eq + 1)..];
            switch (key)
            {
                case "ch" when int.TryParse(value, out int c):
                    channel = c;
                    break;
                case "rate":
                    rate = value;
                    break;
                case "ts" when long.TryParse(value, out long t):
                    ts = t;
                    break;
                case "data":
                    hex = value;
                    break;
            }
        }

        if (channel is null || rate is null || ts is null || hex is null || hex.Length % 2 != 0)
        {
            return false;
        }

        var data = new byte[hex.Length / 2];
        for (int i = 0; i < data.Length; ++i)
        {
            if (!byte.TryParse(hex.AsSpan(i * 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out data[i]))
            {
                return false;
            }
        }

        capture = new RfCapture(channel.Value, rate, ts.Value, data);
        return true;
    }
}
