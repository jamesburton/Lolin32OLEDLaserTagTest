using System.Net;
using System.Net.Sockets;
using System.Text;
using LaserTag.Client.Models;

namespace LaserTag.Client;

/// <summary>
/// Sends <c>CTL</c> lines to the subnet broadcast address over UDP, repeating
/// each message to survive loss (contract: CTL is fire-and-forget). Never use
/// <c>255.255.255.255</c> — devices only receive subnet-directed broadcasts.
/// </summary>
public sealed class UdpControlSender : IControlSender, IDisposable
{
    private readonly UdpMessageParser _parser = new();
    private readonly Func<byte[], CancellationToken, Task> _transmit;
    private readonly int _repeats;
    private readonly TimeSpan _repeatGap;
    private readonly UdpClient? _client;

    /// <summary>
    /// Initializes a production sender that broadcasts to the given endpoint
    /// (subnet broadcast address, port 4210).
    /// </summary>
    /// <param name="broadcastEndpoint">The subnet broadcast endpoint, e.g. 192.168.1.255:4210.</param>
    /// <param name="repeats">How many times each CTL is sent. Defaults to 3.</param>
    /// <param name="repeatGap">Delay between repeats. Defaults to 20 ms.</param>
    public UdpControlSender(IPEndPoint broadcastEndpoint, int repeats = 3, TimeSpan? repeatGap = null)
        : this(repeats, repeatGap)
    {
        ArgumentNullException.ThrowIfNull(broadcastEndpoint);
        _client = new UdpClient { EnableBroadcast = true };
        _client.Connect(broadcastEndpoint);
        _transmit = async (payload, ct) => await _client.SendAsync(payload, ct).ConfigureAwait(false);
    }

    /// <summary>
    /// Initializes a sender with a custom transmit function — intended for
    /// tests, which capture payloads instead of opening a socket.
    /// </summary>
    /// <param name="transmit">Receives each raw payload exactly as it would hit the wire.</param>
    /// <param name="repeats">How many times each CTL is sent. Defaults to 3.</param>
    /// <param name="repeatGap">Delay between repeats. Defaults to 20 ms.</param>
    public UdpControlSender(Func<byte[], CancellationToken, Task> transmit, int repeats = 3, TimeSpan? repeatGap = null)
        : this(repeats, repeatGap)
    {
        ArgumentNullException.ThrowIfNull(transmit);
        _transmit = transmit;
    }

    private UdpControlSender(int repeats, TimeSpan? repeatGap)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(repeats, 1);
        _repeats = repeats;
        _repeatGap = repeatGap ?? TimeSpan.FromMilliseconds(20);
        _transmit = null!; // assigned by every public ctor
    }

    /// <inheritdoc/>
    public async Task SendAsync(Control control, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(control);
        byte[] payload = Encoding.ASCII.GetBytes(_parser.FormatControl(control));
        for (int i = 0; i < _repeats; i++)
        {
            if (i > 0 && _repeatGap > TimeSpan.Zero)
            {
                await Task.Delay(_repeatGap, cancellationToken).ConfigureAwait(false);
            }

            await _transmit(payload, cancellationToken).ConfigureAwait(false);
        }
    }

    /// <inheritdoc/>
    public void Dispose() => _client?.Dispose();
}
