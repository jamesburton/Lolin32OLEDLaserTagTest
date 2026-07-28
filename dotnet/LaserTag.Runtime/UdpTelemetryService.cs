using System.Net;
using System.Net.Sockets;
using System.Text;
using LaserTag.Client;
using Microsoft.Extensions.Hosting;

namespace LaserTag.Runtime;

/// <summary>Listens on UDP 4210 and feeds parsed telemetry to <see cref="GameService"/>.</summary>
/// <param name="game">The game service to feed.</param>
/// <param name="guard">
/// Platform guard held for the listener's lifetime. On Android this is the
/// multicast lock, without which no broadcast ever arrives.
/// </param>
public sealed class UdpTelemetryService(GameService game, IPlatformNetworkGuard guard) : BackgroundService
{
    /// <summary>The devices' telemetry/CTL port.</summary>
    public const int Port = 4210;

    private readonly UdpMessageParser _parser = new();

    /// <summary>True once at least one datagram has been received.</summary>
    /// <remarks>
    /// Lets a UI tell "no devices are powered" apart from "this platform is
    /// dropping broadcasts" — states that look identical from an empty roster.
    /// </remarks>
    public bool HasReceivedAny { get; private set; }

    /// <inheritdoc/>
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        guard.Acquire();
        try
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

                HasReceivedAny = true;
                string line = Encoding.ASCII.GetString(result.Buffer);
                if (_parser.Parse(line) is { } message)
                {
                    game.OnMessage(message);
                }
            }
        }
        finally
        {
            guard.Release();
        }
    }
}
