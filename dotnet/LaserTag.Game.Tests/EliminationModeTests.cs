using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

public class EliminationModeTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();

    private MatchEngine Running(EliminationMode mode, params Heartbeat[] lobby)
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);
        engine.StartMatch(mode, lobby);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();
        _sender.Sent.Clear();
        return engine;
    }

    [Fact]
    public void LastTeamStanding_Wins()
    {
        MatchEngine engine = Running(new EliminationMode(), Msg.Hb("a", 1), Msg.Hb("b", 2), Msg.Hb("c", 2));

        engine.OnMessage(Msg.Hit("b", 1, 2, hpAfter: 0));
        Assert.Equal(MatchPhase.Running, engine.Phase); // c still alive on team 2

        engine.OnMessage(Msg.Hit("c", 1, 2, hpAfter: 0));

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(1, engine.Snapshot().Winner);
    }

    [Fact]
    public void NoRespawns_NoResetEverSent()
    {
        MatchEngine engine = Running(new EliminationMode(), Msg.Hb("a", 1), Msg.Hb("b", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0));

        _clock.Advance(TimeSpan.FromMinutes(2));
        engine.Tick();

        Assert.DoesNotContain(_sender.Sent, c => c.Kind == ControlKind.Reset);
    }

    [Fact]
    public void OfflineDevice_DoesNotCountAsAlive()
    {
        MatchEngine engine = Running(new EliminationMode(), Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.MarkOffline("b");
        engine.Tick(); // team 2 has no alive+online member left

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(1, engine.Snapshot().Winner);
    }

    [Fact]
    public void TimerCap_MostAlivePlayersWins()
    {
        var mode = new EliminationMode(timerCap: TimeSpan.FromMinutes(10));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 1), Msg.Hb("c", 2), Msg.Hb("d", 2));
        engine.OnMessage(Msg.Hit("c", 1, 2, hpAfter: 0));

        _clock.Advance(TimeSpan.FromMinutes(10));
        engine.Tick();

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(1, engine.Snapshot().Winner); // team 1: 2 alive vs team 2: 1
    }
}
