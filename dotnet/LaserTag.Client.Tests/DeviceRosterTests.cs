using LaserTag.Client;
using LaserTag.Client.Models;

namespace LaserTag.Client.Tests;

/// <summary>
/// Roster tests drive a controllable clock to assert online/offline transitions
/// at the contract §4 6000 ms timeout, the rejoined flag (design §6), and
/// per-device independence.
/// </summary>
public sealed class DeviceRosterTests
{
    private DateTimeOffset _now = new(2026, 6, 15, 0, 0, 0, TimeSpan.Zero);

    private DeviceRoster CreateRoster() => new(() => _now);

    private static Heartbeat Beat(string id, int hp = 100) => new()
    {
        Source = "lasertag-matrix",
        Id = id,
        Ip = "192.168.1.24",
        Fw = "2.0.0",
        Team = 2,
        Mode = "team-colours",
        Hp = hp,
        Online = true,
    };

    [Fact]
    public void Ingest_NewHeartbeat_DevicePresentAndOnline()
    {
        DeviceRoster roster = CreateRoster();

        RosterEntry entry = roster.Ingest(Beat("a1b2c3"));

        Assert.Equal("a1b2c3", entry.Id);
        Assert.True(entry.Online);
        Assert.False(entry.Rejoined);
        Assert.True(roster.IsOnline("a1b2c3"));
    }

    [Fact]
    public void FirstHeartbeat_NeverFlagsRejoined()
    {
        DeviceRoster roster = CreateRoster();
        RosterEntry entry = roster.Ingest(Beat("a1b2c3"));
        Assert.False(entry.Rejoined);
    }

    [Fact]
    public void Get_UnknownDevice_ReturnsNull()
    {
        DeviceRoster roster = CreateRoster();
        Assert.Null(roster.Get("nope"));
        Assert.False(roster.IsOnline("nope"));
    }

    [Fact]
    public void AdvanceClockPastTimeout_DeviceGoesOffline()
    {
        DeviceRoster roster = CreateRoster();
        roster.Ingest(Beat("a1b2c3"));

        // Exactly 6000 ms is still online (<= timeout); past it is offline.
        _now += TimeSpan.FromMilliseconds(6000);
        Assert.True(roster.IsOnline("a1b2c3"));

        _now += TimeSpan.FromMilliseconds(1);
        Assert.False(roster.IsOnline("a1b2c3"));

        RosterEntry? entry = roster.Get("a1b2c3");
        Assert.NotNull(entry);
        Assert.False(entry!.Online);
    }

    [Fact]
    public void BeatAfterOffline_GoesOnlineAndFlagsRejoined()
    {
        DeviceRoster roster = CreateRoster();
        roster.Ingest(Beat("a1b2c3"));

        // Let it lapse beyond the timeout.
        _now += TimeSpan.FromMilliseconds(7000);
        Assert.False(roster.IsOnline("a1b2c3"));

        RosterEntry entry = roster.Ingest(Beat("a1b2c3"));
        Assert.True(entry.Online);
        Assert.True(entry.Rejoined);
    }

    [Fact]
    public void ContinuousBeats_NeverFlagRejoined()
    {
        DeviceRoster roster = CreateRoster();
        roster.Ingest(Beat("a1b2c3"));

        // Beats every 2 s, never lapsing past 6 s.
        for (int i = 0; i < 5; i++)
        {
            _now += TimeSpan.FromMilliseconds(2000);
            RosterEntry entry = roster.Ingest(Beat("a1b2c3"));
            Assert.True(entry.Online);
            Assert.False(entry.Rejoined);
        }
    }

    [Fact]
    public void ClearRejoined_ResetsFlag()
    {
        DeviceRoster roster = CreateRoster();
        roster.Ingest(Beat("a1b2c3"));
        _now += TimeSpan.FromMilliseconds(7000);
        roster.Ingest(Beat("a1b2c3"));

        Assert.True(roster.Get("a1b2c3")!.Rejoined);
        Assert.True(roster.ClearRejoined("a1b2c3"));
        Assert.False(roster.Get("a1b2c3")!.Rejoined);
        Assert.False(roster.ClearRejoined("unknown"));
    }

    [Fact]
    public void RejoinedFlag_ClearsWhenDeviceGoesOfflineAgain()
    {
        DeviceRoster roster = CreateRoster();
        roster.Ingest(Beat("a1b2c3"));
        _now += TimeSpan.FromMilliseconds(7000);
        roster.Ingest(Beat("a1b2c3"));
        Assert.True(roster.Get("a1b2c3")!.Rejoined);

        // Lapse again — an offline device is no longer "rejoined".
        _now += TimeSpan.FromMilliseconds(7000);
        RosterEntry? entry = roster.Get("a1b2c3");
        Assert.False(entry!.Online);
        Assert.False(entry.Rejoined);
    }

    [Fact]
    public void MultipleDevices_TrackedIndependently()
    {
        DeviceRoster roster = CreateRoster();
        roster.Ingest(Beat("aaa111"));

        _now += TimeSpan.FromMilliseconds(4000);
        roster.Ingest(Beat("bbb222"));

        // Advance so aaa111 (last seen at t=0) is offline but bbb222 (t=4000) is online.
        _now += TimeSpan.FromMilliseconds(3000); // now = 7000
        Assert.False(roster.IsOnline("aaa111"));
        Assert.True(roster.IsOnline("bbb222"));

        Assert.Equal(2, roster.GetAll().Count);
    }

    [Fact]
    public void GetAll_ReflectsLatestHeartbeatPayload()
    {
        DeviceRoster roster = CreateRoster();
        roster.Ingest(Beat("a1b2c3", hp: 100));
        _now += TimeSpan.FromMilliseconds(2000);
        roster.Ingest(Beat("a1b2c3", hp: 40));

        RosterEntry entry = Assert.Single(roster.GetAll());
        Assert.Equal(40, entry.LastHeartbeat.Hp);
    }

    [Fact]
    public void CustomOfflineTimeout_IsHonoured()
    {
        var roster = new DeviceRoster(() => _now, TimeSpan.FromMilliseconds(1000));
        roster.Ingest(Beat("a1b2c3"));

        _now += TimeSpan.FromMilliseconds(1001);
        Assert.False(roster.IsOnline("a1b2c3"));
    }
}
