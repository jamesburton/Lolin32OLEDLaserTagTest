using System.Net;
using System.Net.Sockets;
using System.Text;
using LaserTag.Client;
using Microsoft.Extensions.Hosting;

namespace LaserTag.Host;

/// <summary>Listens on UDP 4210 and feeds parsed telemetry to <see cref="GameService"/>.</summary>
public sealed class UdpTelemetryService(GameService game) : BackgroundService
{
    /// <summary>The devices' telemetry/CTL port.</summary>
    public const int Port = 4210;

    private readonly UdpMessageParser _parser = new();

    /// <inheritdoc/>
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var client = new UdpClient();
        client.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        client.Client.Bind(new IPEndPoint(IPAddress.Any, Port));

        while (!stoppingToken.IsCancellationRequested)
        {
            UdpReceiveResult result;
            try
            {
                result = await client.ReceiveAsync(stoppingToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (SocketException)
            {
                continue; // transient (e.g. ICMP port-unreachable reflections on Windows)
            }

            string line = Encoding.ASCII.GetString(result.Buffer);
            if (_parser.Parse(line) is { } message)
            {
                game.OnMessage(message);
            }
        }
    }
}
