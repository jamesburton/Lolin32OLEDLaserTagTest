// Throwaway smoke harness for LaserTag.Client — proves the host library works
// against a live device. Reads status+config over REST, then listens for UDP
// heartbeats/telemetry and prints a live roster.
//
//   dotnet run --project dotnet/LaserTag.Smoke -- [ip] [seconds] [--fix-firewall]
//
// Defaults: ip = 192.168.1.24, seconds = 20. --fix-firewall launches the
// tools/setup-firewall script (which self-elevates) and exits. Requires the V2
// control-plane firmware on the device.
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using LaserTag.Client;
using LaserTag.Client.Models;

var argList = args.ToList();
bool fixFirewall = argList.Remove("--fix-firewall");
string ip = argList.Count > 0 ? argList[0] : "192.168.1.24";
int seconds = argList.Count > 1 && int.TryParse(argList[1], out int parsed) ? parsed : 20;

if (fixFirewall)
{
    return LaunchFirewallSetup();
}

Console.WriteLine($"== LaserTag host smoke == device={ip} listen={seconds}s");

// --- REST: read status + config --------------------------------------------
using var http = new HttpClient
{
    BaseAddress = new Uri($"http://{ip}"),
    Timeout = TimeSpan.FromSeconds(5),
};
var client = new LaserTagClient(http);

bool restOk = false;
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
    restOk = true;
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
udp.Client.Bind(new IPEndPoint(IPAddress.Any, NetworkDiagnostics.TelemetryPort));
Console.WriteLine($"Listening on :{NetworkDiagnostics.TelemetryPort} ...");

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
    // REST worked but no heartbeats => almost certainly an inbound firewall block.
    Console.WriteLine(restOk
        ? NetworkDiagnostics.NoHeartbeatHint() + Environment.NewLine +
          "  (or run this harness with --fix-firewall to launch the setup script)"
        : "  (no heartbeats and REST failed — is the device on and flashed with V2 firmware?)");
}

return 0;

// Launch the platform firewall setup script (it self-elevates). Returns an exit code.
int LaunchFirewallSetup()
{
    bool windows = NetworkDiagnostics.CurrentOS() == NetworkDiagnostics.OSKind.Windows;
    string script = windows ? "tools/setup-firewall.ps1" : "tools/setup-firewall.sh";
    if (!File.Exists(script))
    {
        Console.WriteLine($"Could not find {script} from the current directory ({Directory.GetCurrentDirectory()}).");
        Console.WriteLine($"Run it from the repo root, or manually: {NetworkDiagnostics.FirewallFixCommand()}");
        return 1;
    }

    var psi = windows
        ? new ProcessStartInfo("powershell", $"-NoProfile -ExecutionPolicy Bypass -File \"{script}\"")
        : new ProcessStartInfo("bash", $"\"{script}\"");
    psi.UseShellExecute = false;

    Console.WriteLine($"Launching {script} ...");
    using Process? p = Process.Start(psi);
    p?.WaitForExit();
    return p?.ExitCode ?? 1;
}
