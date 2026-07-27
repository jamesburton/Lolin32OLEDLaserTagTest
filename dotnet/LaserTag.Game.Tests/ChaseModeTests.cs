using LaserTag.Client.Models;

namespace LaserTag.Game.Tests;

/// <summary>
/// Drives <see cref="ChaseMode"/> purely through the <see cref="IGameMode"/>
/// surface and a hand-built <see cref="MatchContext"/> (fake clock, recorded
/// sends, live score dictionary) — no engine, no internals.
/// </summary>
public class ChaseModeTests
{
    private readonly FakeClock _clock = new();
    private readonly List<Control> _sent = [];
    private readonly Dictionary<int, int> _scores = [];
    private readonly List<Participant> _participants = [];

    /// <summary>
    /// Builds a context over the harness state. Mirrors the engine: the live
    /// score dictionary is handed over directly so a mutation from
    /// <c>AddScore</c> is visible to a same-call <c>CheckEnd</c>.
    /// </summary>
    /// <returns>The context for the current instant.</returns>
    private MatchContext Ctx() => new(
        now: _clock.Now,
        matchStartedAt: DateTimeOffset.UnixEpoch,
        startHp: 32,
        participants: _participants,
        scores: _scores,
        addScore: (team, pts) => _scores[team] = _scores.GetValueOrDefault(team) + pts,
        send: _sent.Add);

    /// <summary>Enrolls online participants with sequential ids p1..pN.</summary>
    /// <param name="count">How many participants to add.</param>
    private void AddParticipants(int count)
    {
        for (int i = 1; i <= count; i++)
        {
            _participants.Add(new Participant
            {
                Id = $"p{i}", Hostname = $"host{i}", Team = i, Hp = 32, Alive = true, Online = true,
            });
        }
    }

    /// <summary>Replaces a participant record via a mutation.</summary>
    /// <param name="id">The participant id.</param>
    /// <param name="mutate">The mutation to apply.</param>
    private void Mutate(string id, Func<Participant, Participant> mutate)
    {
        int idx = _participants.FindIndex(p => p.Id == id);
        _participants[idx] = mutate(_participants[idx]);
    }

    /// <summary>Gets the id of the single Activate sent so far, clearing the log.</summary>
    /// <returns>The activated device id.</returns>
    private string TakeActivateId()
    {
        Control activate = Assert.Single(_sent, c => c.Kind == ControlKind.Activate);
        _sent.Clear();
        return activate.Id!;
    }

    /// <summary>
    /// Starts the mode and runs the opening gap so exactly one target is
    /// active, returning its id.
    /// </summary>
    /// <param name="mode">The mode under test.</param>
    /// <param name="gap">The configured gap (advanced past).</param>
    /// <returns>The first activated device id.</returns>
    private string StartAndActivate(ChaseMode mode, TimeSpan gap)
    {
        mode.OnMatchStart(Ctx());
        _sent.Clear();
        _clock.Advance(gap);
        mode.OnTick(Ctx());
        return TakeActivateId();
    }

    /// <summary>Times the active target out and activates the next one.</summary>
    /// <param name="mode">The mode under test.</param>
    /// <param name="activeId">The currently active device id.</param>
    /// <param name="gap">The configured gap.</param>
    /// <returns>The next activated device id.</returns>
    private string TimeoutAndActivateNext(ChaseMode mode, string activeId, TimeSpan gap)
    {
        Participant active = _participants.First(p => p.Id == activeId);
        mode.OnDeviceState(Ctx(), new StateEvent { Source = active.Hostname, S = "timeout", Ts = 1 }, active);
        _clock.Advance(gap);
        mode.OnTick(Ctx());
        return TakeActivateId();
    }

    [Fact]
    public void OnMatchStart_SendsChaseOn_ThenFirstActivateAfterGap()
    {
        AddParticipants(3);
        var mode = new ChaseMode(TimeSpan.FromMinutes(5), null, penalty: 1, display: "dark");

        mode.OnMatchStart(Ctx());

        Control on = Assert.Single(_sent);
        Assert.Equal(ControlKind.ChaseOn, on.Kind);
        Assert.Equal(1, on.Penalty);
        Assert.Equal("dark", on.Display);
        _sent.Clear();

        // Half a gap in: still dark, nothing on the wire.
        _clock.Advance(TimeSpan.FromMilliseconds(500));
        mode.OnTick(Ctx());
        Assert.Empty(_sent);

        _clock.Advance(TimeSpan.FromMilliseconds(500));
        mode.OnTick(Ctx());

        Control activate = Assert.Single(_sent);
        Assert.Equal(ControlKind.Activate, activate.Kind);
        Assert.Contains(_participants, p => p.Id == activate.Id);
        Assert.InRange(activate.T!.Value, 2000, 5000);
    }

    [Fact]
    public void Hit_OnActiveTarget_ScoresShooterTeam_AndSchedulesGap()
    {
        AddParticipants(3);
        var mode = new ChaseMode(TimeSpan.FromMinutes(5), null);
        string target = StartAndActivate(mode, TimeSpan.FromSeconds(1));

        mode.OnHit(Ctx(), Hit(target, shooterTeam: 3));

        Assert.Equal(1, _scores[3]);

        // The next round waits out the full gap.
        _clock.Advance(TimeSpan.FromMilliseconds(999));
        mode.OnTick(Ctx());
        Assert.Empty(_sent);

        _clock.Advance(TimeSpan.FromMilliseconds(1));
        mode.OnTick(Ctx());
        Assert.Single(_sent, c => c.Kind == ControlKind.Activate);
    }

    [Fact]
    public void Hit_OnOtherDevice_WhileActive_DoesNotScore()
    {
        AddParticipants(3);
        var mode = new ChaseMode(TimeSpan.FromMinutes(5), null);
        string target = StartAndActivate(mode, TimeSpan.FromSeconds(1));
        string other = _participants.First(p => p.Id != target).Id;

        mode.OnHit(Ctx(), Hit(other, shooterTeam: 3));

        Assert.Empty(_scores);

        // The round did not end: no gap was scheduled, and hitting the real
        // target still scores.
        mode.OnTick(Ctx());
        Assert.Empty(_sent);
        mode.OnHit(Ctx(), Hit(target, shooterTeam: 3));
        Assert.Equal(1, _scores[3]);
    }

    [Fact]
    public void DormantHit_WithPenalty_DeductsFlooredAtZero()
    {
        AddParticipants(3);
        var mode = new ChaseMode(TimeSpan.FromMinutes(5), null, penalty: 1);
        string target = StartAndActivate(mode, TimeSpan.FromSeconds(1));
        string other = _participants.First(p => p.Id != target).Id;

        // Nothing scored yet: the penalty must floor at zero, never go negative.
        mode.OnHit(Ctx(), Hit(other, shooterTeam: 3, dormant: true));
        Assert.Equal(0, _scores.GetValueOrDefault(3));

        _scores[3] = 2;
        mode.OnHit(Ctx(), Hit(other, shooterTeam: 3, dormant: true));
        Assert.Equal(1, _scores[3]);
    }

    [Fact]
    public void DormantHit_WithoutPenalty_Ignored()
    {
        AddParticipants(3);
        var mode = new ChaseMode(TimeSpan.FromMinutes(5), null);
        string target = StartAndActivate(mode, TimeSpan.FromSeconds(1));
        string other = _participants.First(p => p.Id != target).Id;
        _scores[3] = 2;

        mode.OnHit(Ctx(), Hit(other, shooterTeam: 3, dormant: true));

        Assert.Equal(2, _scores[3]);

        // A dormant hit on the active board is still not a score.
        mode.OnHit(Ctx(), Hit(target, shooterTeam: 3, dormant: true));
        Assert.Equal(2, _scores[3]);
        Assert.Empty(_sent);
    }

    [Fact]
    public void Timeout_State_AdvancesWithoutScoring()
    {
        AddParticipants(3);
        var mode = new ChaseMode(TimeSpan.FromMinutes(5), null);
        string first = StartAndActivate(mode, TimeSpan.FromSeconds(1));

        string next = TimeoutAndActivateNext(mode, first, TimeSpan.FromSeconds(1));

        Assert.Empty(_scores);
        Assert.NotEqual(first, next); // 3 boards: no immediate repeat
    }

    [Fact]
    public void SlackExpiry_WithoutTimeoutEvt_DeactivatesAndAdvances()
    {
        AddParticipants(3);
        var mode = new ChaseMode(TimeSpan.FromMinutes(5), null);
        string first = StartAndActivate(mode, TimeSpan.FromSeconds(1));

        // Neither EVT hit nor EVT state timeout arrives. Max window (5 s) plus
        // the 1.5 s slack is the worst case; past it the host takes over.
        _clock.Advance(TimeSpan.FromMilliseconds(6600));
        mode.OnTick(Ctx());

        Control deactivate = Assert.Single(_sent);
        Assert.Equal(ControlKind.Deactivate, deactivate.Kind);
        Assert.Equal(first, deactivate.Id);
        _sent.Clear();
        Assert.Empty(_scores);

        _clock.Advance(TimeSpan.FromSeconds(1));
        mode.OnTick(Ctx());
        Assert.NotEqual(first, TakeActivateId());
    }

    [Fact]
    public void ActiveTarget_GoingOffline_AdvancesImmediately()
    {
        AddParticipants(3);
        var mode = new ChaseMode(TimeSpan.FromMinutes(5), null);
        string first = StartAndActivate(mode, TimeSpan.FromSeconds(1));

        Mutate(first, p => p with { Online = false });
        mode.OnTick(Ctx()); // well inside the window, but the target is gone

        Control deactivate = Assert.Single(_sent);
        Assert.Equal(ControlKind.Deactivate, deactivate.Kind);
        Assert.Equal(first, deactivate.Id);
        _sent.Clear();

        _clock.Advance(TimeSpan.FromSeconds(1));
        mode.OnTick(Ctx());
        Assert.NotEqual(first, TakeActivateId());
    }

    [Fact]
    public void TwoBoards_AllowImmediateRepeat_ThreeBoardsNever()
    {
        var gap = TimeSpan.FromSeconds(1);

        // Two boards: alternation would be perfectly predictable, so the pick
        // stays uniform and a repeat is allowed (and, over 20 rounds with a
        // seeded rng, certain to occur).
        AddParticipants(2);
        var two = new ChaseMode(TimeSpan.FromMinutes(30), null, rng: new Random(1234));
        List<string> twoPicks = RunRounds(two, gap, 20);
        Assert.Contains(Enumerable.Range(1, twoPicks.Count - 1), i => twoPicks[i] == twoPicks[i - 1]);

        // Three boards: the previous target is excluded from the pool.
        _participants.Clear();
        _sent.Clear();
        AddParticipants(3);
        var three = new ChaseMode(TimeSpan.FromMinutes(30), null, rng: new Random(1234));
        List<string> threePicks = RunRounds(three, gap, 20);
        Assert.All(
            Enumerable.Range(1, threePicks.Count - 1),
            i => Assert.NotEqual(threePicks[i - 1], threePicks[i]));
    }

    [Fact]
    public void FirstTo_EndsWithWinner_DurationEndsWithLeader_TieIsDraw()
    {
        AddParticipants(2);

        // first-to only: no duration, ends the instant a team reaches the target.
        var firstTo = new ChaseMode(null, 3);
        _scores[1] = 2;
        Assert.Null(firstTo.CheckEnd(Ctx()));
        _scores[1] = 3;
        Assert.Equal(1, firstTo.CheckEnd(Ctx())!.WinnerTeam);

        // Duration only: the leader at expiry wins.
        _scores.Clear();
        var timed = new ChaseMode(TimeSpan.FromMinutes(1), null);
        _scores[1] = 2;
        _scores[2] = 5;
        Assert.Null(timed.CheckEnd(Ctx()));
        _clock.Advance(TimeSpan.FromMinutes(1));
        Assert.Equal(2, timed.CheckEnd(Ctx())!.WinnerTeam);

        // Level scores at expiry are a draw, as is a match with no score at all.
        _scores[1] = 5;
        Assert.Equal(0, timed.CheckEnd(Ctx())!.WinnerTeam);
        _scores.Clear();
        Assert.Equal(0, timed.CheckEnd(Ctx())!.WinnerTeam);
    }

    [Fact]
    public void ScoreboardModeParticipant_IsNeverActivated()
    {
        AddParticipants(3);
        Mutate("p2", p => p with { Mode = "scoreboard" });
        var mode = new ChaseMode(TimeSpan.FromMinutes(30), null, rng: new Random(7));

        List<string> picks = RunRounds(mode, TimeSpan.FromSeconds(1), 20);

        Assert.DoesNotContain("p2", picks);
        Assert.Equal(20, picks.Count);
    }

    [Fact]
    public void Constructor_RequiresDurationOrFirstTo()
    {
        Assert.Throws<ArgumentException>(() => new ChaseMode(null, null));
    }

    /// <summary>Builds a hit event against a victim device.</summary>
    /// <param name="victim">The victim device id.</param>
    /// <param name="shooterTeam">The firing team.</param>
    /// <param name="dormant">Whether the victim was dormant when hit.</param>
    /// <returns>The hit event.</returns>
    private static HitEvent Hit(string victim, int shooterTeam, bool dormant = false) => new()
    {
        Source = "host", Victim = victim, ShooterTeam = shooterTeam, Dmg = 1,
        Proto = "vatos", Hp = 32, Ts = 1000, Dormant = dormant,
    };

    /// <summary>
    /// Runs a match through <paramref name="rounds"/> timeout-terminated
    /// rounds, collecting the activated device id of each.
    /// </summary>
    /// <param name="mode">The mode under test.</param>
    /// <param name="gap">The configured gap.</param>
    /// <param name="rounds">How many rounds to run.</param>
    /// <returns>The activated ids, in order.</returns>
    private List<string> RunRounds(ChaseMode mode, TimeSpan gap, int rounds)
    {
        List<string> picks = [StartAndActivate(mode, gap)];
        for (int i = 1; i < rounds; i++)
        {
            picks.Add(TimeoutAndActivateNext(mode, picks[^1], gap));
        }

        return picks;
    }
}
