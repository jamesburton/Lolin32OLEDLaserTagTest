using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

/// <summary>
/// Team 0 (<see cref="Teams.None"/>) is a neutral target: shootable by
/// everyone, scoring for the shooter, never a side that can win.
/// </summary>
public class NeutralTeamTests
{
    private readonly FakeControlSender _sender = new();
    private readonly FakeClock _clock = new();

    private MatchEngine Running(IGameMode mode, params Heartbeat[] lobby)
    {
        var engine = new MatchEngine(_sender, () => _clock.Now);
        engine.StartMatch(mode, lobby);
        _clock.Advance(TimeSpan.FromSeconds(5));
        engine.Tick();
        _sender.Sent.Clear();
        return engine;
    }

    [Theory]
    [InlineData(0, true)]
    [InlineData(1, true)]
    [InlineData(4, true)]
    [InlineData(-1, false)]
    [InlineData(5, false)]
    public void IsValid_AcceptsNoneThroughFour(int team, bool expected) =>
        Assert.Equal(expected, Teams.IsValid(team));

    [Theory]
    [InlineData("none", 0)]
    [InlineData("NEUTRAL", 0)]
    [InlineData("0", 0)]
    [InlineData("3", 3)]
    public void TryParse_AcceptsNamesAndNumbers(string text, int expected)
    {
        Assert.True(Teams.TryParse(text, out int team));
        Assert.Equal(expected, team);
    }

    [Theory]
    [InlineData("5")]
    [InlineData("-1")]
    [InlineData("red")]
    [InlineData("")]
    [InlineData(null)]
    public void TryParse_RejectsOutOfRangeAndNonsense(string? text) =>
        Assert.False(Teams.TryParse(text, out _));

    [Fact]
    public void Describe_RendersNeutralAsNone()
    {
        Assert.Equal("none", Teams.Describe(Teams.None));
        Assert.Equal("2", Teams.Describe(2));
    }

    [Fact]
    public void NeutralTarget_IsShootableAndScoresForTheShooter()
    {
        MatchEngine engine = Running(
            new DeathmatchMode(TimeSpan.FromMinutes(5)),
            Msg.Hb("prop", Teams.None),
            Msg.Hb("a", 1));

        engine.OnMessage(Msg.Hit("prop", shooterTeam: 1, dmg: 2, hpAfter: 30));

        Assert.Equal(1, engine.Snapshot().TeamScores[1]);
    }

    [Fact]
    public void NeutralTarget_NeverBecomesAScoringTeam()
    {
        MatchEngine engine = Running(
            new DeathmatchMode(TimeSpan.FromMinutes(5)),
            Msg.Hb("prop", Teams.None),
            Msg.Hb("a", 1));

        // A malformed/spoofed event claiming shooter team 0 must not open a
        // "team none" bucket — it could otherwise go on to win the match.
        engine.OnMessage(Msg.Hit("a", shooterTeam: Teams.None, dmg: 2, hpAfter: 30));

        Assert.DoesNotContain(Teams.None, engine.Snapshot().TeamScores.Keys);
    }

    [Fact]
    public void Elimination_NeutralTargetIsNotTheLastTeamStanding()
    {
        MatchEngine engine = Running(
            new EliminationMode(),
            Msg.Hb("prop", Teams.None),
            Msg.Hb("a", 1),
            Msg.Hb("b", 2));

        // Team 2 is wiped out. Only team 1 remains as a real side, so the
        // round must end with team 1 winning — the surviving neutral prop is
        // not a rival side keeping the round alive.
        engine.OnMessage(Msg.Hit("b", shooterTeam: 1, dmg: 32, hpAfter: 0));

        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(1, engine.Snapshot().Winner);
    }

    [Fact]
    public void Elimination_AllNeutralLobbyEndsAsADrawNotANeutralWin()
    {
        MatchEngine engine = Running(
            new EliminationMode(),
            Msg.Hb("prop1", Teams.None),
            Msg.Hb("prop2", Teams.None));

        engine.Tick();

        // No real side is present, so there is nothing to win: a draw (0),
        // never "team none wins".
        Assert.Equal(MatchPhase.Finished, engine.Phase);
        Assert.Equal(0, engine.Snapshot().Winner);
    }

    [Fact]
    public void Participant_IsNeutralReflectsTeamZero()
    {
        MatchEngine engine = Running(
            new DeathmatchMode(TimeSpan.FromMinutes(5)),
            Msg.Hb("prop", Teams.None),
            Msg.Hb("a", 3));

        var byId = engine.Snapshot().Participants.ToDictionary(p => p.Id, StringComparer.Ordinal);
        Assert.True(byId["prop"].IsNeutral);
        Assert.False(byId["a"].IsNeutral);
    }
}
