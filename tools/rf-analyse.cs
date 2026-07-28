#:project ../dotnet/LaserTag.Rf/LaserTag.Rf.csproj
#:property GenerateDocumentationFile=true

// Offline analysis of RF probe captures. A .NET 10 file-based app: run it
// directly, no project scaffolding.
//
//   dotnet run tools/rf-analyse.cs docs/captures/<file>.txt
//
// Reads `RF ch= rate= ts= n= data=` lines produced by `sniff` on the
// esp8266-rfprobe firmware, and reports per channel/rate how many captures
// survive CRC validation as Enhanced ShockBurst packets. Only a non-zero
// CRC-valid count is a detection; occupancy figures never are.

using LaserTag.Rf;

string path = args.Length > 0 ? args[0] : "rf-captures.txt";
var captures = new List<RfCapture>();
foreach (string line in File.ReadLines(path))
{
    if (RfLineParser.TryParse(line.Trim(), out RfCapture c))
    {
        captures.Add(c);
    }
}

Console.WriteLine($"parsed {captures.Count} captures from {path}");

foreach (var group in captures
    .GroupBy(c => (c.Channel, c.Rate))
    .OrderBy(g => g.Key.Channel)
    .ThenBy(g => g.Key.Rate))
{
    // Try every plausible nRF24 address width: a wrong width fails CRC, so this
    // costs nothing and rules out "we assumed 5 bytes and it was 3".
    var valid = new List<(int Width, ValidatedPacket Packet)>();
    foreach (RfCapture c in group)
    {
        for (int width = 3; width <= 5; ++width)
        {
            if (PacketValidator.TryValidate(c.Data, width, out ValidatedPacket p))
            {
                valid.Add((width, p));
                break;
            }
        }
    }

    Console.WriteLine($"ch={group.Key.Channel} ({2400 + group.Key.Channel} MHz) rate={group.Key.Rate}: "
                      + $"{group.Count()} captures, {valid.Count} CRC-valid");
    foreach ((int width, ValidatedPacket packet) in valid.Take(5))
    {
        Console.WriteLine($"    aw={width} addr={Convert.ToHexString(packet.Address)} "
                          + $"pid={packet.Pid} shift={packet.BitShift} "
                          + $"payload={Convert.ToHexString(packet.Payload)}");
    }
}

Console.WriteLine();
Console.WriteLine("recurring 5-byte sequences across all captures (top 5):");
Console.WriteLine("(AAAAAAAAAA / 5555555555 / A0A0A0A0A0 and similar are noise, not addresses)");
foreach (AddressCandidate a in AddressRecovery.FindCandidates(captures, 5, 3).Take(5))
{
    Console.WriteLine($"    {Convert.ToHexString(a.Address)}  seen in {a.Occurrences} captures");
}
