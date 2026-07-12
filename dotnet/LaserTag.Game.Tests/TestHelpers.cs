using LaserTag.Client;
using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

/// <summary>Captures every control the engine sends, in order.</summary>
public sealed class FakeControlSender : IControlSender
{
    public List<Control> Sent { get; } = [];

    public Task SendAsync(Control control, CancellationToken cancellationToken = default)
    {
        Sent.Add(control);
        return Task.CompletedTask;
    }
}

/// <summary>A manually advanced clock.</summary>
public sealed class FakeClock
{
    public DateTimeOffset Now { get; private set; } = DateTimeOffset.UnixEpoch;

    public void Advance(TimeSpan by) => Now += by;
}

public static class Msg
{
    public static Heartbeat Hb(string id, int team, int hp = 32, string host = "host") => new()
    {
        Source = host, Id = id, Ip = "192.168.1.10", Fw = "2.0.0",
        Team = team, Mode = "idle", Hp = hp, Online = true,
    };

    public static HitEvent Hit(string victim, int shooterTeam, int dmg, int hpAfter, string host = "host") => new()
    {
        Source = host, Victim = victim, ShooterTeam = shooterTeam,
        Dmg = dmg, Proto = "vatos", Hp = hpAfter, Ts = 1000,
    };
}

/// <summary>A do-nothing mode for lifecycle tests.</summary>
public sealed class NullMode : IGameMode
{
    public string Name => "null";

    public TimeSpan? MatchDuration => null;

    public void OnMatchStart(MatchContext context)
    {
    }

    public void OnHit(MatchContext context, HitEvent hit)
    {
    }

    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant)
    {
    }

    public void OnTick(MatchContext context)
    {
    }

    public MatchResult? CheckEnd(MatchContext context) => null;
}
