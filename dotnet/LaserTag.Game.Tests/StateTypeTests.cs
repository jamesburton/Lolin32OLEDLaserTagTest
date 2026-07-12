using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

public class StateTypeTests
{
    [Fact]
    public void MatchContext_AddScore_AccumulatesPerTeam()
    {
        var sent = new List<Control>();
        var scores = new Dictionary<int, int>();
        var ctx = new MatchContext(
            now: DateTimeOffset.UnixEpoch,
            matchStartedAt: DateTimeOffset.UnixEpoch,
            startHp: 32,
            participants: [],
            scores: scores,
            addScore: (team, pts) => scores[team] = scores.GetValueOrDefault(team) + pts,
            send: sent.Add);

        ctx.AddScore(2, 1);
        ctx.AddScore(2, 5);
        ctx.Send(new Control { Kind = ControlKind.Stop });

        Assert.Equal(6, ctx.Scores[2]);
        Assert.Single(sent);
    }
}
