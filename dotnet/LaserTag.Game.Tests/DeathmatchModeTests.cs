using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

public class DeathmatchModeTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();

    private MatchEngine Running(DeathmatchMode mode, params Heartbeat[] lobby)
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);
        engine.StartMatch(mode, lobby);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();
        _sender.Sent.Clear();
        return engine;
    }

    [Fact]
    public void Hit_ScoresHitPoints_KillScoresBoth()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(5));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.OnMessage(Msg.Hit("a", shooterTeam: 2, dmg: 2, hpAfter: 30));
        engine.OnMessage(Msg.Hit("a", shooterTeam: 2, dmg: 2, hpAfter: 0)); // kill

        Assert.Equal(1 + (1 + 5), engine.Snapshot().TeamScores[2]);
    }

    [Fact]
    public void DeadPlayer_RespawnsAfterDelay_ViaAddressedReset()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(5), respawnDelay: TimeSpan.FromSeconds(10));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0));

        _clock.Advance(TimeSpan.FromSeconds(9));
        engine.Tick();
        Assert.DoesNotContain(_sender.Sent, c => c.Kind == ControlKind.Reset);

        _clock.Advance(TimeSpan.FromSeconds(1));
        engine.Tick();

        Control reset = Assert.Single(_sender.Sent, c => c.Kind == ControlKind.Reset);
        Assert.Equal("a", reset.Id);
        Assert.Equal(32, reset.Hp);

        // The respawn is not re-sent on subsequent ticks.
        engine.Tick();
        Assert.Single(_sender.Sent, c => c.Kind == ControlKind.Reset);
    }

    [Fact]
    public void WaveMode_RespawnsAllDeadOnTheInterval()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(5), waveInterval: TimeSpan.FromSeconds(30));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2), Msg.Hb("c", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0));
        engine.OnMessage(Msg.Hit("b", 1, 2, hpAfter: 0));

        _clock.Advance(TimeSpan.FromSeconds(30));
        engine.Tick();

        List<Control> resets = _sender.Sent.Where(c => c.Kind == ControlKind.Reset).ToList();
        Assert.Equal(2, resets.Count);
        Assert.Equal(new[] { "a", "b" }, resets.Select(r => r.Id).Order().ToArray());
    }

    [Fact]
    public void TimerExpiry_FinishesWithHighestScoringTeam()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(1));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 30));

        _clock.Advance(TimeSpan.FromMinutes(1));
        engine.Tick();

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(2, engine.Snapshot().Winner);
    }

    [Fact]
    public void TimerExpiry_WithTiedScores_IsADraw()
    {
        var mode = new DeathmatchMode(TimeSpan.FromMinutes(1));
        MatchEngine engine = Running(mode, Msg.Hb("a", 1), Msg.Hb("b", 2));

        _clock.Advance(TimeSpan.FromMinutes(1));
        engine.Tick();

        Assert.Equal(0, engine.Snapshot().Winner);
    }
}
