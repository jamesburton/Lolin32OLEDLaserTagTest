using System.Net;
using System.Text;
using LaserTag.Client.Models;

namespace LaserTag.Client.Tests;

public class UdpControlSenderTests
{
    [Fact]
    public async Task SendAsync_RepeatsPayloadThreeTimesByDefault()
    {
        var sent = new List<string>();
        var sender = new UdpControlSender(
            (payload, _) =>
            {
                sent.Add(Encoding.ASCII.GetString(payload));
                return Task.CompletedTask;
            },
            repeatGap: TimeSpan.Zero);

        await sender.SendAsync(new Control { Kind = ControlKind.Stop });

        Assert.Equal(3, sent.Count);
        Assert.All(sent, s => Assert.Equal("CTL stop", s));
    }

    [Fact]
    public async Task SendAsync_HonoursConfiguredRepeatCount()
    {
        int count = 0;
        var sender = new UdpControlSender((_, _) => { count++; return Task.CompletedTask; }, repeats: 4, repeatGap: TimeSpan.Zero);

        await sender.SendAsync(new Control { Kind = ControlKind.Start });

        Assert.Equal(4, count);
    }

    [Theory]
    [InlineData("192.168.1.59", "255.255.255.0", "192.168.1.255")]
    [InlineData("10.20.30.40", "255.255.0.0", "10.20.255.255")]
    [InlineData("172.16.5.9", "255.255.255.128", "172.16.5.127")]
    public void Compute_DerivesSubnetBroadcast(string address, string mask, string expected)
    {
        IPAddress result = BroadcastAddress.Compute(IPAddress.Parse(address), IPAddress.Parse(mask));
        Assert.Equal(IPAddress.Parse(expected), result);
    }
}
