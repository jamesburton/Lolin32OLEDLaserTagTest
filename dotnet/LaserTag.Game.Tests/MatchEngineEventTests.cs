using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

/// <summary>Records mode callback invocations.</summary>
public sealed class RecordingMode : IGameMode
{
    public List<HitEvent> Hits { get; } = [];

    public List<StateEvent> States { get; } = [];

    public string Name => "recording";

    public TimeSpan? MatchDuration => null;

    public void OnMatchStart(MatchContext context)
    {
    }

    public void OnHit(MatchContext context, HitEvent hit) => Hits.Add(hit);

    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant) => States.Add(state);

    public void OnTick(MatchContext context)
    {
    }

    public MatchResult? CheckEnd(MatchContext context) => null;
}

/// <summary>Ends the match as soon as any team reaches 3 points.</summary>
public sealed class FirstToThreeMode : IGameMode
{
    public string Name => "first-to-three";

    public TimeSpan? MatchDuration => null;

    public void OnMatchStart(MatchContext context)
    {
    }

    public void OnHit(MatchContext context, HitEvent hit) => context.AddScore(hit.ShooterTeam, 1);

    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant)
    {
    }

    public void OnTick(MatchContext context)
    {
    }

    public MatchResult? CheckEnd(MatchContext context) =>
        context.Scores.Where(kv => kv.Value >= 3).Select(kv => (MatchResult?)new MatchResult(kv.Key)).FirstOrDefault();
}

public class MatchEngineEventTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();
    private readonly RecordingMode _mode = new();

    private MatchEngine RunningEngine(params Heartbeat[] lobby)
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);
        engine.StartMatch(_mode, lobby);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();
        _sender.Sent.Clear();
        return engine;
    }

    [Fact]
    public void Hit_UpdatesVictimAndNotifiesMode()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.OnMessage(Msg.Hit(victim: "a", shooterTeam: 2, dmg: 2, hpAfter: 30));

        Participant a = engine.Snapshot().Participants.Single(p => p.Id == "a");
        Assert.Equal(30, a.Hp);
        Assert.True(a.Alive);
        Assert.Single(_mode.Hits);
    }

    [Fact]
    public void FatalHit_MarksDeadWithDiedAt()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0));

        Participant a = engine.Snapshot().Participants.Single(p => p.Id == "a");
        Assert.False(a.Alive);
        Assert.Equal(_clock.Now, a.DiedAt);
    }

    [Fact]
    public void Hit_FromUnknownDevice_IsIgnored()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1));

        engine.OnMessage(Msg.Hit("ghost", 2, 2, 0));

        Assert.Empty(_mode.Hits);
    }

    [Fact]
    public void Hit_OutsideRunning_IsIgnored()
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);

        engine.OnMessage(Msg.Hit("a", 2, 2, 0)); // Lobby phase

        Assert.Empty(_mode.Hits);
        Assert.Equal(MatchPhase.Lobby, engine.Phase);
    }

    [Fact]
    public void Heartbeat_ReconcilesHpDrop_WithoutScoring()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));

        // The EVT hit was lost; the next HB shows hp=0.
        engine.OnMessage(Msg.Hb("a", 1, hp: 0));

        Participant a = engine.Snapshot().Participants.Single(p => p.Id == "a");
        Assert.False(a.Alive);
        Assert.Empty(_mode.Hits); // reconciliation never scores
    }

    [Fact]
    public void Heartbeat_AfterOffline_ReissuesStartToThatDevice()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));

        engine.MarkOffline("a"); // roster says it dropped
        engine.OnMessage(Msg.Hb("a", 1, hp: 32));

        Control reissue = Assert.Single(_sender.Sent);
        Assert.Equal(ControlKind.Start, reissue.Kind);
        Assert.Equal("a", reissue.Id);
        Assert.True(engine.Snapshot().Participants.Single(p => p.Id == "a").Online);
    }

    [Fact]
    public void Heartbeat_AfterOfflineWhileDead_SendsResetNotStart_StaysDead()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1), Msg.Hb("b", 2));
        engine.OnMessage(Msg.Hit("a", 2, 2, hpAfter: 0)); // dead before going offline
        _sender.Sent.Clear();

        engine.MarkOffline("a");
        engine.OnMessage(Msg.Hb("a", 1, hp: 0)); // rejoin heartbeat still reports dead

        Control reissue = Assert.Single(_sender.Sent);
        Assert.Equal(ControlKind.Reset, reissue.Kind);
        Assert.Equal(0, reissue.Hp);
        Assert.Equal("a", reissue.Id);
        Participant a = engine.Snapshot().Participants.Single(p => p.Id == "a");
        Assert.False(a.Alive);
        Assert.True(a.Online);
    }

    [Fact]
    public void StateEvent_UpdatesHpAndNotifiesMode()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1, host: "lasertag-a"));

        engine.OnMessage(new StateEvent { Source = "lasertag-a", S = "respawn", Hp = 32, Ts = 2000 });

        Assert.Single(_mode.States);
        Assert.Equal(32, engine.Snapshot().Participants.Single().Hp);
    }

    [Fact]
    public void CheckEnd_SeesScoreAddedBySameEvent()
    {
        var mode = new FirstToThreeMode();
        var engine = new MatchEngine(_sender, () => _clock.Now);
        engine.StartMatch(mode, [Msg.Hb("a", 1), Msg.Hb("b", 2)]);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();
        _sender.Sent.Clear();

        engine.OnMessage(Msg.Hit("a", 2, 1, hpAfter: 29));
        engine.OnMessage(Msg.Hit("a", 2, 1, hpAfter: 26));
        engine.OnMessage(Msg.Hit("a", 2, 1, hpAfter: 23));

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(2, engine.Snapshot().Winner);
    }

    [Fact]
    public void StateEvent_FatalHp_MarksDeadWithDiedAt()
    {
        MatchEngine engine = RunningEngine(Msg.Hb("a", 1, host: "lasertag-a"));

        engine.OnMessage(new StateEvent { Source = "lasertag-a", S = "dead", Hp = 0, Ts = 2000 });

        Participant a = engine.Snapshot().Participants.Single();
        Assert.False(a.Alive);
        Assert.Equal(_clock.Now, a.DiedAt);
    }
}
