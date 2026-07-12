using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

public class MatchEngineLifecycleTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();

    private MatchEngine NewEngine() => new(_sender, () => _clock.Now);

    [Fact]
    public void StartMatch_EntersCountdown_AndSendsCountdownCue()
    {
        MatchEngine engine = NewEngine();

        engine.StartMatch(new NullMode(), [Msg.Hb("a", team: 1), Msg.Hb("b", team: 2)]);

        Assert.Equal(MatchPhase.Countdown, engine.Phase);
        Control cue = Assert.Single(_sender.Sent);
        Assert.Equal(ControlKind.Countdown, cue.Kind);
        Assert.Equal(5, cue.N);
        Assert.Equal(2, engine.Snapshot().Participants.Count);
    }

    [Fact]
    public void Tick_AfterCountdownElapses_StartsRunning_SendsStartAndReset()
    {
        MatchEngine engine = NewEngine();
        engine.StartMatch(new NullMode(), [Msg.Hb("a", 1)]);

        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();

        Assert.Equal(MatchPhase.Running, engine.Phase);
        Assert.Equal(ControlKind.Start, _sender.Sent[1].Kind);
        Assert.Equal(ControlKind.Reset, _sender.Sent[2].Kind);
        Assert.Equal(32, _sender.Sent[2].Hp);
        Assert.All(engine.Snapshot().Participants, p => Assert.True(p.Alive && p.Hp == 32));
    }

    [Fact]
    public void Stop_DuringRunning_FinishesWithModeResultOrDraw()
    {
        MatchEngine engine = NewEngine();
        engine.StartMatch(new NullMode(), [Msg.Hb("a", 1)]);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();

        engine.Stop();

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Control last = _sender.Sent[^1];
        Assert.Equal(ControlKind.GameOver, last.Kind);
        Assert.Equal(0, last.Winner); // NullMode never yields a result → draw
        Assert.Equal(0, engine.Snapshot().Winner);
    }

    [Fact]
    public void StartMatch_WhenNotInLobbyOrFinished_Throws()
    {
        MatchEngine engine = NewEngine();
        engine.StartMatch(new NullMode(), [Msg.Hb("a", 1)]);

        Assert.Throws<InvalidOperationException>(
            () => engine.StartMatch(new NullMode(), [Msg.Hb("a", 1)]));
    }

    [Fact]
    public void StartMatch_WithNoDevices_Throws()
    {
        Assert.Throws<InvalidOperationException>(() => NewEngine().StartMatch(new NullMode(), []));
    }
}
