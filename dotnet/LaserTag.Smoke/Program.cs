// Throwaway smoke harness for LaserTag.Client — proves the host library works
// against a live device. Reads status+config over REST, then listens for UDP
// heartbeats/telemetry and prints a live roster.
//
//   dotnet run --project dotnet/LaserTag.Smoke -- [ip] [seconds]
//
// Defaults: ip = 192.168.1.24, seconds = 20. Requires the V2 control-plane
// firmware on the device (old firmware emits the legacy telemetry format and
// has no /api/* routes).
using System.Net;
using System.Net.Sockets;
using System.Text;
using LaserTag.Client;
using LaserTag.Client.Models;

string ip = args.Length > 0 ? args[0] : "192.168.1.24";
int seconds = args.Length > 1 && int.TryParse(args[1], out int s) ? s : 20;

Console.WriteLine($"== LaserTag host smoke == device={ip} listen={seconds}s");

// --- REST: read status + config -------------------------------------------
using var http = new HttpClient
{
    BaseAddress = new Uri($"http://{ip}"),
    Timeout = TimeSpan.FromSeconds(5),
};
var client = new LaserTagClient(http);

try
{
    StatusDoc status = await client.GetStatusAsync();
    Console.WriteLine(
        $"STATUS  device={status.DeviceId} fw={status.Fw} mode={status.Mode} " +
        $"team={status.OwnTeam} hp={status.Hp} online={status.Online} " +
        $"uptime={status.UptimeMs}ms rssi={status.Rssi}");

    ConfigDoc cfg = await client.GetConfigAsync();
    Console.WriteLine(
        $"CONFIG  ownTeam={cfg.OwnTeam} enabled=[{string.Join(",", cfg.EnabledTeams)}] " +
        $"proto={cfg.ProtocolId} brightness={cfg.Brightness} " +
        $"colours={{{string.Join(",", cfg.TeamColours.Select(kv => $"{kv.Key}:{kv.Value}"))}}}");
}
catch (Exception ex)
{
    Console.WriteLine($"REST error (is the V2 firmware flashed and the device reachable?): {ex.Message}");
}

// --- UDP: listen + build a live roster -------------------------------------
var parser = new UdpMessageParser();
var roster = new DeviceRoster(() => DateTimeOffset.UtcNow);

using var udp = new UdpClient();
udp.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
udp.Client.Bind(new IPEndPoint(IPAddress.Any, 4210));
Console.WriteLine($"Listening on :4210 ...");

using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(seconds));
try
{
    while (!cts.IsCancellationRequested)
    {
        UdpReceiveResult res = await udp.ReceiveAsync(cts.Token);
        string line = Encoding.ASCII.GetString(res.Buffer).TrimEnd('\r', '\n');
        switch (parser.Parse(line))
        {
            case Heartbeat hb:
                RosterEntry e = roster.Ingest(hb);
                Console.WriteLine($"  HB    {hb.Id} ip={hb.Ip} team={hb.Team} mode={hb.Mode} hp={hb.Hp} (online={e.Online} rejoined={e.Rejoined})");
                break;
            case HitEvent h:
                Console.WriteLine($"  HIT   victim={h.Victim} shooterTeam={h.ShooterTeam} dmg={h.Dmg} proto={h.Proto} hp={h.Hp} ts={h.Ts}");
                break;
            case StateEvent st:
                Console.WriteLine($"  STATE {st.Source} s={st.S} hp={(st.Hp?.ToString() ?? "-")} ts={st.Ts}");
                break;
            default:
                if (line.Length > 0)
                {
                    Console.WriteLine($"  (unparsed) {line}");
                }

                break;
        }
    }
}
catch (OperationCanceledException)
{
    // listen window elapsed
}

Console.WriteLine("--- Roster ---");
foreach (RosterEntry e in roster.GetAll())
{
    Console.WriteLine($"  {e.Id}  online={e.Online} rejoined={e.Rejoined} team={e.LastHeartbeat.Team} hp={e.LastHeartbeat.Hp} lastSeen={e.LastSeen:HH:mm:ss}");
}

if (roster.GetAll().Count == 0)
{
    Console.WriteLine("  (no heartbeats seen — flash the V2 firmware, and check you are on the same subnet)");
}
