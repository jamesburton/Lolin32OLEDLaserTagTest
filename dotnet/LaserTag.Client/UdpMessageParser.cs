using System.Globalization;
using System.Text;
using LaserTag.Client.Models;

namespace LaserTag.Client;

/// <summary>
/// Parses raw UDP lines emitted by devices into typed messages, and formats
/// outbound <see cref="Control"/> messages into their exact on-wire form. The
/// grammar and golden vectors are defined in the wire contract §1.
/// </summary>
/// <remarks>
/// The parser is deliberately tolerant: any line that does not match a known
/// grammar is dropped (returns <see langword="null"/>) rather than throwing,
/// matching the lossy fire-and-forget nature of the UDP telemetry channel
/// (design §8). Inbound lines carry a leading hostname token (contract §1.1);
/// outbound <c>CTL</c> lines do not and are NOT parsed by this host-side parser.
/// </remarks>
public sealed class UdpMessageParser
{
    private static readonly char[] Whitespace = [' ', '\t'];

    /// <summary>
    /// Parses a single raw UDP line into a typed inbound message.
    /// </summary>
    /// <param name="line">
    /// The raw line as received. A trailing newline is tolerated. The first
    /// whitespace-delimited token is taken as the source hostname.
    /// </param>
    /// <returns>
    /// A <see cref="Heartbeat"/>, <see cref="HitEvent"/> or <see cref="StateEvent"/>,
    /// or <see langword="null"/> if the line is empty, malformed, partial, or of
    /// an unknown class. Never throws for malformed input.
    /// </returns>
    public UdpInboundMessage? Parse(string? line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return null;
        }

        // Trim a single trailing newline / carriage return and any surrounding
        // whitespace; the contract allows lines with or without a trailing "\n".
        string trimmed = line.Trim();

        string[] tokens = trimmed.Split(Whitespace, StringSplitOptions.RemoveEmptyEntries);
        if (tokens.Length < 2)
        {
            // Need at least a source token and a class token.
            return null;
        }

        string source = tokens[0];
        string messageClass = tokens[1];

        return messageClass switch
        {
            "HB" => ParseHeartbeat(source, tokens, fieldStart: 2),
            "EVT" => ParseEvent(source, tokens),
            _ => null,
        };
    }

    private static UdpInboundMessage? ParseEvent(string source, string[] tokens)
    {
        if (tokens.Length < 3)
        {
            // "EVT" with no subtype.
            return null;
        }

        string subtype = tokens[2];
        return subtype switch
        {
            "hit" => ParseHit(source, tokens, fieldStart: 3),
            "state" => ParseState(source, tokens, fieldStart: 3),
            _ => null,
        };
    }

    private static Heartbeat? ParseHeartbeat(string source, string[] tokens, int fieldStart)
    {
        Dictionary<string, string> fields = ParseFields(tokens, fieldStart);

        if (!fields.TryGetValue("id", out string? id) ||
            !fields.TryGetValue("ip", out string? ip) ||
            !fields.TryGetValue("fw", out string? fw) ||
            !TryGetInt(fields, "team", out int team) ||
            !fields.TryGetValue("mode", out string? mode) ||
            !TryGetInt(fields, "hp", out int hp) ||
            !TryGetInt(fields, "online", out int online))
        {
            return null;
        }

        return new Heartbeat
        {
            Source = source,
            Id = id,
            Ip = ip,
            Fw = fw,
            Team = team,
            Mode = mode,
            Hp = hp,
            Online = online != 0,
        };
    }

    private static HitEvent? ParseHit(string source, string[] tokens, int fieldStart)
    {
        Dictionary<string, string> fields = ParseFields(tokens, fieldStart);

        if (!fields.TryGetValue("victim", out string? victim) ||
            !TryGetInt(fields, "shooterTeam", out int shooterTeam) ||
            !TryGetInt(fields, "dmg", out int dmg) ||
            !fields.TryGetValue("proto", out string? proto) ||
            !TryGetInt(fields, "hp", out int hp) ||
            !TryGetLong(fields, "ts", out long ts))
        {
            return null;
        }

        return new HitEvent
        {
            Source = source,
            Victim = victim,
            ShooterTeam = shooterTeam,
            Dmg = dmg,
            Proto = proto,
            Hp = hp,
            Ts = ts,
            Dormant = fields.TryGetValue("dormant", out string? dormant) && dormant == "1",
        };
    }

    private static StateEvent? ParseState(string source, string[] tokens, int fieldStart)
    {
        Dictionary<string, string> fields = ParseFields(tokens, fieldStart);

        if (!fields.TryGetValue("s", out string? s) ||
            !TryGetLong(fields, "ts", out long ts))
        {
            return null;
        }

        int? hp = null;
        if (fields.TryGetValue("hp", out string? hpRaw))
        {
            if (!int.TryParse(hpRaw, NumberStyles.Integer, CultureInfo.InvariantCulture, out int hpValue))
            {
                // hp present but not a valid int → malformed, drop.
                return null;
            }

            hp = hpValue;
        }

        return new StateEvent
        {
            Source = source,
            S = s,
            Hp = hp,
            Ts = ts,
        };
    }

    /// <summary>
    /// Parses the trailing <c>key=value</c> tokens into a dictionary. Unknown
    /// keys are retained here and simply ignored by callers (contract §1.1).
    /// Tokens without an <c>=</c> are skipped.
    /// </summary>
    private static Dictionary<string, string> ParseFields(string[] tokens, int start)
    {
        var fields = new Dictionary<string, string>(StringComparer.Ordinal);
        for (int i = start; i < tokens.Length; i++)
        {
            string token = tokens[i];
            int eq = token.IndexOf('=');
            if (eq <= 0)
            {
                // No key, or empty key — not a valid key=value token; ignore.
                continue;
            }

            string key = token[..eq];
            string value = token[(eq + 1)..];
            fields[key] = value;
        }

        return fields;
    }

    private static bool TryGetInt(Dictionary<string, string> fields, string key, out int value)
    {
        value = 0;
        return fields.TryGetValue(key, out string? raw) &&
               int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
    }

    private static bool TryGetLong(Dictionary<string, string> fields, string key, out long value)
    {
        value = 0;
        return fields.TryGetValue(key, out string? raw) &&
               long.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>
    /// Formats an outbound <see cref="Control"/> message into the exact
    /// host→device <c>CTL ...</c> wire string (contract §1.4). The result has NO
    /// hostname prefix and no trailing newline.
    /// </summary>
    /// <param name="control">The control message to format.</param>
    /// <returns>The exact on-wire string, e.g. <c>CTL start ts=30000</c>.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// Thrown if <see cref="Control.Kind"/> is not a recognized value.
    /// </exception>
    public string FormatControl(Control control)
    {
        ArgumentNullException.ThrowIfNull(control);

        var sb = new StringBuilder("CTL ");
        switch (control.Kind)
        {
            case ControlKind.Start:
                sb.Append("start");
                if (control.Ts is { } ts)
                {
                    sb.Append(" ts=").Append(ts.ToString(CultureInfo.InvariantCulture));
                }

                break;

            case ControlKind.Stop:
                sb.Append("stop");
                break;

            case ControlKind.Reset:
                sb.Append("reset");
                if (control.Hp is { } hp)
                {
                    sb.Append(" hp=").Append(hp.ToString(CultureInfo.InvariantCulture));
                }

                break;

            case ControlKind.Countdown:
                sb.Append("countdown");
                if (control.N is { } n)
                {
                    sb.Append(" n=").Append(n.ToString(CultureInfo.InvariantCulture));
                }

                break;

            case ControlKind.GameOver:
                sb.Append("gameover");
                if (control.Winner is { } winner)
                {
                    sb.Append(" winner=").Append(winner.ToString(CultureInfo.InvariantCulture));
                }

                break;

            case ControlKind.Activate:
                sb.Append("activate");
                if (control.T is { } t)
                {
                    sb.Append(" t=").Append(t.ToString(CultureInfo.InvariantCulture));
                }

                break;

            case ControlKind.Deactivate:
                sb.Append("deactivate");
                break;

            case ControlKind.ChaseOn:
                sb.Append("chase on");
                sb.Append(" penalty=").Append((control.Penalty ?? 0).ToString(CultureInfo.InvariantCulture));
                sb.Append(" display=").Append(control.Display ?? "score");
                break;

            case ControlKind.ChaseOff:
                sb.Append("chase off");
                break;

            case ControlKind.Score:
                sb.Append("score");
                for (int team = 1; team <= 4; team++)
                {
                    int pts = control.Scores?.GetValueOrDefault(team) ?? 0;
                    sb.Append(' ').Append(team).Append('=')
                      .Append(pts.ToString(CultureInfo.InvariantCulture));
                }

                break;

            default:
                throw new ArgumentOutOfRangeException(
                    nameof(control),
                    control.Kind,
                    "Unknown control kind.");
        }

        // Grammar v2 addressing: id= is always the last key when present.
        if (control.Id is { } targetId)
        {
            sb.Append(" id=").Append(targetId);
        }

        return sb.ToString();
    }

    /// <summary>
    /// Parses a host→device <c>CTL ...</c> wire line back into a
    /// <see cref="Control"/>. The inverse of <see cref="FormatControl"/>.
    /// </summary>
    /// <param name="line">The raw CTL line (no hostname prefix; trailing newline tolerated).</param>
    /// <returns>
    /// The parsed control, or <see langword="null"/> if the line is empty,
    /// malformed, has a hostname prefix, or names an unknown verb. Never throws.
    /// </returns>
    public Control? ParseControl(string? line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return null;
        }

        string[] tokens = line.Trim().Split(Whitespace, StringSplitOptions.RemoveEmptyEntries);
        if (tokens.Length < 2 || tokens[0] != "CTL")
        {
            return null;
        }

        ControlKind kind;
        int fieldStart = 2;
        switch (tokens[1])
        {
            case "start": kind = ControlKind.Start; break;
            case "stop": kind = ControlKind.Stop; break;
            case "reset": kind = ControlKind.Reset; break;
            case "countdown": kind = ControlKind.Countdown; break;
            case "gameover": kind = ControlKind.GameOver; break;
            case "activate": kind = ControlKind.Activate; break;
            case "deactivate": kind = ControlKind.Deactivate; break;
            case "score": kind = ControlKind.Score; break;
            case "chase":
                // "chase" carries a subtype token (on/off) before any k=v
                // fields; peek it and shift the field-parsing start past it.
                if (tokens.Length < 3)
                {
                    return null;
                }

                switch (tokens[2])
                {
                    case "on": kind = ControlKind.ChaseOn; break;
                    case "off": kind = ControlKind.ChaseOff; break;
                    default: return null;
                }

                fieldStart = 3;
                break;
            default: return null;
        }

        Dictionary<string, string> fields = ParseFields(tokens, fieldStart);

        long? ts = null;
        if (fields.ContainsKey("ts"))
        {
            if (!TryGetLong(fields, "ts", out long tsValue))
            {
                return null;
            }

            ts = tsValue;
        }

        int? hp = null, n = null, winner = null;
        if (fields.ContainsKey("hp"))
        {
            if (!TryGetInt(fields, "hp", out int v))
            {
                return null;
            }

            hp = v;
        }

        if (fields.ContainsKey("n"))
        {
            if (!TryGetInt(fields, "n", out int v))
            {
                return null;
            }

            n = v;
        }

        if (fields.ContainsKey("winner"))
        {
            if (!TryGetInt(fields, "winner", out int v))
            {
                return null;
            }

            winner = v;
        }

        int? t = null;
        if (fields.ContainsKey("t"))
        {
            if (!TryGetInt(fields, "t", out int v))
            {
                return null;
            }

            t = v;
        }

        int? penalty = null;
        if (fields.ContainsKey("penalty"))
        {
            if (!TryGetInt(fields, "penalty", out int v))
            {
                return null;
            }

            penalty = v;
        }

        fields.TryGetValue("display", out string? display);

        Dictionary<int, int>? scores = null;
        if (kind == ControlKind.Score)
        {
            // Grammar v2.1 requires all four team keys; a missing or
            // malformed one is a malformed CTL line, matching the existing
            // "bad int → null" pattern for other verbs.
            var s = new Dictionary<int, int>();
            for (int team = 1; team <= 4; team++)
            {
                if (!TryGetInt(fields, team.ToString(CultureInfo.InvariantCulture), out int pts))
                {
                    return null;
                }

                s[team] = pts;
            }

            scores = s;
        }

        fields.TryGetValue("id", out string? id);

        return new Control
        {
            Kind = kind,
            Ts = ts,
            Hp = hp,
            N = n,
            Winner = winner,
            Id = id,
            T = t,
            Penalty = penalty,
            Display = display,
            Scores = scores,
        };
    }
}
