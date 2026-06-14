using System.Net;
using System.Net.Sockets;
using System.Text;

namespace TagMonitor;

/// <summary>
/// Listens for TagNet UDP telemetry broadcasts from the laser-tag boards and
/// prints each event with a timestamp and source address. Events are device-name
/// prefixed lines such as "lasertag-matrix hit team=1(Blue) dmg=2".
/// </summary>
internal static class Program
{
    private static int Main(string[] args)
    {
        int port = args.Length > 0 && int.TryParse(args[0], out int p) ? p : 4210;

        using var udp = new UdpClient(AddressFamily.InterNetwork);
        udp.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        try
        {
            udp.Client.Bind(new IPEndPoint(IPAddress.Any, port));
        }
        catch (SocketException ex)
        {
            Console.Error.WriteLine($"Failed to bind UDP {port}: {ex.Message}");
            return 1;
        }

        Console.WriteLine($"TagMonitor listening on UDP {port} (Ctrl+C to quit)");
        var remote = new IPEndPoint(IPAddress.Any, 0);

        while (true)
        {
            byte[] data;
            try
            {
                data = udp.Receive(ref remote);
            }
            catch (SocketException)
            {
                break;
            }

            string line = Encoding.UTF8.GetString(data).TrimEnd();
            Print(remote.Address, line);
        }

        return 0;
    }

    private static void Print(IPAddress source, string line)
    {
        // A hit/state change is the interesting signal — colour it.
        ConsoleColor colour = line.Contains(" hit ") ? ConsoleColor.Green
            : line.Contains(" dark") ? ConsoleColor.DarkGray
            : line.Contains(" tx ") ? ConsoleColor.Yellow
            : ConsoleColor.Gray;

        Console.ForegroundColor = colour;
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] {source,-15} {line}");
        Console.ResetColor();
    }
}
