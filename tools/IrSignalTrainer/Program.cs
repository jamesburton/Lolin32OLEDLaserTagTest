using System.Collections.Concurrent;
using System.Globalization;
using System.IO.Ports;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace IrSignalTrainer;

/// <summary>
/// A decoded NEC code: a 16-bit address identifying the device and an 8-bit
/// command identifying the button.
/// </summary>
/// <param name="Addr">16-bit NEC address.</param>
/// <param name="Cmd">8-bit NEC command.</param>
public readonly record struct NecCode(int Addr, int Cmd)
{
    /// <summary>Canonical string form, e.g. <c>0707:04</c>.</summary>
    public override string ToString() => $"{Addr:X4}:{Cmd:X2}";

    /// <summary>
    /// Parses a firmware <c>NEC addr=0x.. cmd=0x..</c> line.
    /// </summary>
    /// <param name="line">A raw serial line.</param>
    /// <returns>The parsed code, or <see langword="null"/> if not an NEC line.</returns>
    public static NecCode? Parse(string line)
    {
        if (!line.StartsWith("NEC ", StringComparison.Ordinal))
        {
            return null;
        }

        int? addr = ExtractHex(line, "addr=0x");
        int? cmd = ExtractHex(line, "cmd=0x");
        return addr is { } a && cmd is { } c ? new NecCode(a, c) : null;
    }

    private static int? ExtractHex(string line, string key)
    {
        int idx = line.IndexOf(key, StringComparison.Ordinal);
        if (idx < 0)
        {
            return null;
        }

        int start = idx + key.Length;
        int end = start;
        while (end < line.Length && Uri.IsHexDigit(line[end]))
        {
            end++;
        }

        return end > start && int.TryParse(line.AsSpan(start, end - start), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out int value)
            ? value
            : null;
    }
}

/// <summary>
/// A single IR event from the board: either a cleanly decoded NEC code, a raw
/// pulse frame for fingerprinting, or both. The firmware emits an optional
/// <c>NEC ..</c> line immediately followed by a <c>FRAME ..</c> line; the
/// FRAME line terminates the event and any preceding NEC attaches to it.
/// </summary>
public sealed record IrEvent(NecCode? Nec, IReadOnlyList<int> MarksUs, string RawSig, int EdgeCount)
{
    /// <summary>Whether this event carries a decoded NEC code.</summary>
    public bool IsNec => Nec is not null;

    /// <summary>A short human-readable description for logging.</summary>
    public string Describe() => Nec is { } code
        ? $"NEC {code}"
        : $"raw[{EdgeCount}] {RawSig}";
}

/// <summary>
/// A trained signature for one button on one device. Stores an NEC code when
/// the signal decodes cleanly, otherwise a quantised full-frame bit signature
/// (every edge classified short/long) for exact non-NEC matching.
/// </summary>
public sealed class Signature
{
    /// <summary>Name of the device the signal came from (e.g. "BlueGun").</summary>
    public required string Device { get; init; }

    /// <summary>Name of the button/weapon on the device.</summary>
    public required string Button { get; init; }

    /// <summary>Canonical NEC code (e.g. "0707:04"), or null for raw signatures.</summary>
    public string? NecCode { get; init; }

    /// <summary>
    /// Quantised full-frame bit signature (e.g. "1000...|0000..."), for non-NEC
    /// signals like the gun where bursts and gaps both carry payload.
    /// </summary>
    public string? RawSig { get; init; }

    /// <summary>Number of sample frames averaged/agreed into this signature.</summary>
    public int Samples { get; init; }

    /// <summary>True if this is a code-based (NEC) signature.</summary>
    [JsonIgnore]
    public bool IsNec => NecCode is not null;
}

/// <summary>
/// Loads and saves the signature library as JSON.
/// </summary>
public sealed class SignatureStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    private readonly string path;

    /// <summary>
    /// Initializes the store, loading any existing library from disk.
    /// </summary>
    /// <param name="path">Path of the JSON library file.</param>
    public SignatureStore(string path)
    {
        this.path = path;
        if (File.Exists(path))
        {
            Signatures = JsonSerializer.Deserialize<List<Signature>>(File.ReadAllText(path), JsonOptions) ?? [];
        }
    }

    /// <summary>All trained signatures.</summary>
    public List<Signature> Signatures { get; } = [];

    /// <summary>Adds or replaces the signature for a device/button pair and saves.</summary>
    /// <param name="signature">The signature to store.</param>
    public void Upsert(Signature signature)
    {
        Signatures.RemoveAll(s => s.Device == signature.Device && s.Button == signature.Button);
        Signatures.Add(signature);
        Save();
    }

    private void Save() => File.WriteAllText(path, JsonSerializer.Serialize(Signatures, JsonOptions));
}

/// <summary>
/// Matches an incoming event against trained signatures: exact NEC-code match
/// when available, otherwise an exact quantised full-frame signature match.
/// </summary>
public static class EventMatcher
{
    /// <summary>
    /// Finds the matching signature for an event, if any.
    /// </summary>
    /// <param name="signatures">The trained signature library.</param>
    /// <param name="ev">The incoming event.</param>
    /// <returns>The match (with a 0 deviation score — matches are exact), or null.</returns>
    public static (Signature Signature, double Score)? FindBest(IEnumerable<Signature> signatures, IrEvent ev)
    {
        List<Signature> list = signatures as List<Signature> ?? signatures.ToList();

        // Exact NEC-code match takes priority — robust and unambiguous
        if (ev.Nec is { } code)
        {
            string key = code.ToString();
            Signature? exact = list.FirstOrDefault(s => s.NecCode == key);
            return exact is null ? null : (exact, 0.0);
        }

        // Non-NEC: exact match on the quantised full-frame bit signature
        Signature? raw = list.FirstOrDefault(s => s.RawSig is not null && s.RawSig == ev.RawSig);
        return raw is null ? null : (raw, 0.0);
    }
}

/// <summary>
/// Console entry point: connects to the board's serial output, shows live
/// match results, and provides a training mode to tag signals against known
/// devices and buttons.
/// </summary>
public static class Program
{
    private const int TrainingSamples = 4;

    // Non-NEC frames with fewer marks than this are treated as fragments/noise
    // (e.g. the NEC repeat burst) and ignored. Real gun frames are far larger.
    private const int MinRawMarks = 4;

    // Quantisation threshold (µs) splitting short (0) from long (1) symbols when
    // building a raw full-frame signature. The Vatos gun uses ~380/~800µs.
    private const int RawQuantThresholdUs = 600;

    private static readonly ConcurrentQueue<IrEvent> Events = new();

    /// <summary>
    /// Runs the trainer.
    /// </summary>
    /// <param name="args">Optional: serial port name (default COM14) and library path (default signatures.json).</param>
    public static int Main(string[] args)
    {
        string portName = args.Length > 0 ? args[0] : "COM14";
        string libraryPath = args.Length > 1 ? args[1] : "signatures.json";

        var store = new SignatureStore(libraryPath);
        Console.WriteLine($"IR Signal Trainer — {portName} @115200, library: {libraryPath} ({store.Signatures.Count} signatures)");
        Console.WriteLine("Commands: [t]rain  [l]ist  [q]uit — fire a device to see live matches");
        Console.WriteLine();

        using var port = new SerialPort(portName, 115200);
        try
        {
            port.Open();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Failed to open {portName}: {ex.Message}");
            Console.Error.WriteLine($"Available ports: {string.Join(", ", SerialPort.GetPortNames())}");
            return 1;
        }

        using var cts = new CancellationTokenSource();
        Task reader = Task.Run(() => ReadSerial(port, cts.Token), cts.Token);

        while (!cts.IsCancellationRequested)
        {
            while (Events.TryDequeue(out IrEvent? ev))
            {
                ShowLiveMatch(store, ev);
            }

            if (Console.KeyAvailable)
            {
                switch (char.ToLowerInvariant(Console.ReadKey(intercept: true).KeyChar))
                {
                    case 'q':
                        cts.Cancel();
                        break;
                    case 't':
                        Train(store);
                        break;
                    case 'l':
                        ListSignatures(store);
                        break;
                }
            }

            Thread.Sleep(50);
        }

        try
        {
            reader.Wait(TimeSpan.FromSeconds(2));
        }
        catch (AggregateException)
        {
            // Reader cancellation is expected on shutdown
        }

        return 0;
    }

    private static void ReadSerial(SerialPort port, CancellationToken token)
    {
        port.ReadTimeout = 500;
        NecCode? pendingNec = null;

        while (!token.IsCancellationRequested)
        {
            string line;
            try
            {
                line = port.ReadLine().Trim();
            }
            catch (TimeoutException)
            {
                continue; // No data within ReadTimeout — loop and check cancellation
            }

            // NEC lines precede their FRAME line; stash and attach on the frame
            if (NecCode.Parse(line) is { } code)
            {
                pendingNec = code;
                continue;
            }

            if (TryParseFrame(line, out List<int> marks, out string rawSig, out int edgeCount))
            {
                // Suppress tiny non-NEC fragments (e.g. the NEC repeat burst from
                // a held button). Real signals — including the gun — are larger.
                if (pendingNec is not null || marks.Count >= MinRawMarks)
                {
                    Events.Enqueue(new IrEvent(pendingNec, marks, rawSig, edgeCount));
                }

                pendingNec = null;
            }
        }
    }

    private static bool TryParseFrame(string line, out List<int> marks, out string rawSig, out int edgeCount)
    {
        marks = [];
        rawSig = string.Empty;
        edgeCount = 0;
        if (!line.StartsWith("FRAME ", StringComparison.Ordinal))
        {
            return false;
        }

        int dataIdx = line.IndexOf("data=", StringComparison.Ordinal);
        if (dataIdx < 0)
        {
            return false;
        }

        string[] tokens = line[(dataIdx + 5)..].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

        // The raw signature quantises every edge (bursts and gaps, in order) to a
        // short/long bit, capturing the full frame — non-NEC weapons differ in
        // their bursts, which a gap-only fingerprint would miss.
        var sig = new StringBuilder(tokens.Length);
        foreach (string token in tokens)
        {
            if (token.Length < 2 || !int.TryParse(token.AsSpan(1), out int us))
            {
                return false; // malformed token — discard the whole line
            }

            sig.Append(us > RawQuantThresholdUs ? '1' : '0');
            if (token[0] == 'H')
            {
                marks.Add(us);
            }
        }

        rawSig = sig.ToString();
        edgeCount = tokens.Length;
        return marks.Count > 0;
    }

    private static void ShowLiveMatch(SignatureStore store, IrEvent ev)
    {
        if (EventMatcher.FindBest(store.Signatures, ev) is { } match)
        {
            Console.ForegroundColor = ConsoleColor.Green;
            string detail = ev.Nec is { } code ? code.ToString() : "raw";
            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] HIT  {match.Signature.Device} / {match.Signature.Button}  ({detail})");
        }
        else
        {
            Console.ForegroundColor = ConsoleColor.DarkYellow;
            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Unknown: {ev.Describe()}");
        }

        Console.ResetColor();
    }

    private static void Train(SignatureStore store)
    {
        Console.Write("Device name: ");
        string device = Console.ReadLine()?.Trim() ?? string.Empty;
        Console.Write("Button name: ");
        string button = Console.ReadLine()?.Trim() ?? string.Empty;
        if (device.Length == 0 || button.Length == 0)
        {
            Console.WriteLine("Cancelled.");
            return;
        }

        Console.WriteLine($"Fire/press '{button}' on '{device}' {TrainingSamples} times (60s timeout)...");
        while (Events.TryDequeue(out _))
        {
            // Discard any events captured before training started
        }

        var samples = new List<IrEvent>();
        DateTime deadline = DateTime.UtcNow.AddSeconds(60);
        while (samples.Count < TrainingSamples && DateTime.UtcNow < deadline)
        {
            if (Events.TryDequeue(out IrEvent? ev))
            {
                samples.Add(ev);
                Console.WriteLine($"  sample {samples.Count}/{TrainingSamples}: {ev.Describe()}");
            }
            else
            {
                Thread.Sleep(50);
            }
        }

        if (samples.Count == 0)
        {
            Console.WriteLine("No signals captured — check the sensor and try again.");
            return;
        }

        Signature signature = BuildSignature(device, button, samples);
        store.Upsert(signature);
        string what = signature.NecCode is { } code ? $"NEC {code}" : $"raw {signature.RawSig}";
        Console.WriteLine($"Saved '{device} / {button}' from {signature.Samples} samples: {what}");
    }

    private static Signature BuildSignature(string device, string button, List<IrEvent> samples)
    {
        // Prefer a decoded NEC code: take the most frequently agreed code
        var necGroups = samples
            .Where(s => s.Nec is not null)
            .GroupBy(s => s.Nec!.Value.ToString())
            .OrderByDescending(g => g.Count())
            .ToList();

        if (necGroups.Count > 0)
        {
            return new Signature
            {
                Device = device,
                Button = button,
                NecCode = necGroups[0].Key,
                Samples = necGroups[0].Count(),
            };
        }

        // Fall back to a raw full-frame signature: take the most frequently
        // agreed quantised pattern so an occasional noisy capture doesn't win.
        var rawGroups = samples
            .GroupBy(s => s.RawSig)
            .OrderByDescending(g => g.Count())
            .ToList();

        return new Signature
        {
            Device = device,
            Button = button,
            RawSig = rawGroups[0].Key,
            Samples = rawGroups[0].Count(),
        };
    }

    private static void ListSignatures(SignatureStore store)
    {
        if (store.Signatures.Count == 0)
        {
            Console.WriteLine("No signatures trained yet — press 't' to train one.");
            return;
        }

        foreach (Signature sig in store.Signatures.OrderBy(s => s.Device).ThenBy(s => s.Button))
        {
            string what = sig.NecCode is { } code ? $"NEC {code}" : $"raw {sig.RawSig}";
            Console.WriteLine($"  {sig.Device} / {sig.Button}: {what} ({sig.Samples} samples)");
        }
    }
}
