using LaserTag.Client.Models;
using LaserTag.Game;

namespace LaserTag.Ui.Tests;

/// <summary>
/// Exercises <see cref="ModeFactory.TryCreate"/> — the single gate shared by
/// the web form, the Android form and the HTTP API. Every accepted or
/// rejected request here reflects the actual behaviour a UI or client sees.
/// </summary>
public class ModeFactoryTests
{
    [Fact]
    public void Dm_WithValidDuration_ProducesDeathmatchMode()
    {
        var request = new MatchRequest { Mode = "dm", Duration = "5m" };

        bool ok = ModeFactory.TryCreate(request, out IGameMode mode, out string error);

        Assert.True(ok);
        Assert.Empty(error);
        Assert.Equal("deathmatch", mode.Name);
        Assert.Equal(TimeSpan.FromMinutes(5), mode.MatchDuration);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("banana")]
    public void Dm_WithMissingOrGarbageDuration_FailsWithHelpfulError(string? duration)
    {
        var request = new MatchRequest { Mode = "dm", Duration = duration };

        bool ok = ModeFactory.TryCreate(request, out _, out string error);

        Assert.False(ok);
        // A guessable, specific error, not a generic "invalid request".
        Assert.False(string.IsNullOrWhiteSpace(error));
        Assert.Contains("duration", error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Dm_WithoutHitOrKillPoints_StillCreates_DefaultsApplied()
    {
        // HitPoints/KillPoints null must fall back to 1/5 (DeathmatchMode's own
        // defaults) rather than the factory forwarding a null incorrectly.
        // Not directly observable on the built mode, so this only guards
        // successful creation with the omitted fields — see DeathmatchModeTests
        // for the scoring behaviour of the 1/5 defaults themselves.
        var request = new MatchRequest { Mode = "dm", Duration = "5m", HitPoints = null, KillPoints = null };

        bool ok = ModeFactory.TryCreate(request, out IGameMode mode, out string error);

        Assert.True(ok);
        Assert.Equal(string.Empty, error);
        Assert.Equal("deathmatch", mode.Name);
    }

    [Fact]
    public void Elim_WithoutTimer_AlwaysSucceeds()
    {
        var request = new MatchRequest { Mode = "elim" };

        bool ok = ModeFactory.TryCreate(request, out IGameMode mode, out string error);

        Assert.True(ok);
        Assert.Empty(error);
        Assert.Equal("elimination", mode.Name);
        Assert.Null(mode.MatchDuration);
    }

    [Fact]
    public void Elim_WithTimer_AlwaysSucceeds()
    {
        var request = new MatchRequest { Mode = "elim", Timer = "10m" };

        bool ok = ModeFactory.TryCreate(request, out IGameMode mode, out string error);

        Assert.True(ok);
        Assert.Empty(error);
        Assert.Equal("elimination", mode.Name);
        Assert.Equal(TimeSpan.FromMinutes(10), mode.MatchDuration);
    }

    [Fact]
    public void Elim_WithGarbageTimer_StillSucceeds_TimerTreatedAsAbsent()
    {
        // ParseOptional treats invalid text as "no timer" rather than an
        // error — elimination has no required fields at all.
        var request = new MatchRequest { Mode = "elim", Timer = "garbage" };

        bool ok = ModeFactory.TryCreate(request, out IGameMode mode, out string error);

        Assert.True(ok);
        Assert.Empty(error);
        Assert.Null(mode.MatchDuration);
    }

    [Fact]
    public void Chase_WithNeitherDurationNorFirstTo_Fails()
    {
        // The important case: a chase match with no duration and no first-to
        // target would never end. This must be rejected before it ever
        // reaches ChaseMode's own constructor guard.
        var request = new MatchRequest { Mode = "chase", Duration = null, FirstTo = null };

        bool ok = ModeFactory.TryCreate(request, out _, out string error);

        Assert.False(ok);
        Assert.False(string.IsNullOrWhiteSpace(error));
    }

    [Fact]
    public void Chase_WithOnlyDuration_Succeeds()
    {
        var request = new MatchRequest { Mode = "chase", Duration = "10m", FirstTo = null };

        bool ok = ModeFactory.TryCreate(request, out IGameMode mode, out string error);

        Assert.True(ok);
        Assert.Empty(error);
        Assert.Equal("chase", mode.Name);
        Assert.Equal(TimeSpan.FromMinutes(10), mode.MatchDuration);
    }

    [Fact]
    public void Chase_WithOnlyFirstTo_Succeeds()
    {
        var request = new MatchRequest { Mode = "chase", Duration = null, FirstTo = 5 };

        bool ok = ModeFactory.TryCreate(request, out IGameMode mode, out string error);

        Assert.True(ok);
        Assert.Empty(error);
        Assert.Equal("chase", mode.Name);
        Assert.Null(mode.MatchDuration);
    }

    [Fact]
    public void Chase_WithBothDurationAndFirstTo_Succeeds()
    {
        var request = new MatchRequest { Mode = "chase", Duration = "10m", FirstTo = 5 };

        bool ok = ModeFactory.TryCreate(request, out IGameMode mode, out string error);

        Assert.True(ok);
        Assert.Empty(error);
        Assert.Equal("chase", mode.Name);
        Assert.Equal(TimeSpan.FromMinutes(10), mode.MatchDuration);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("nonsense")]
    public void UnknownOrBlankMode_FailsWithErrorNamingValidModes(string? modeName)
    {
        // MatchRequest.Mode is non-nullable in the type system, but a JSON
        // body with a null/missing "mode" field deserializes to null in
        // practice, so the factory must handle it defensively too.
        var request = new MatchRequest { Mode = modeName!, Duration = "5m" };

        bool ok = ModeFactory.TryCreate(request, out _, out string error);

        Assert.False(ok);
        Assert.Contains("dm", error, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("elim", error, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("chase", error, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData(false, "score")]
    [InlineData(true, "dark")]
    public void Chase_DarkFlag_MapsToExpectedDisplay(bool dark, string expectedDisplay)
    {
        // Dark isn't exposed on ChaseMode as a property, but it is observable
        // through the Control sent on OnMatchStart — the same control the
        // devices actually receive.
        var request = new MatchRequest { Mode = "chase", Duration = "5m", Dark = dark };
        ModeFactory.TryCreate(request, out IGameMode mode, out _);

        Control? sent = null;
        MatchContext context = BuildContext(c => sent = c);
        mode.OnMatchStart(context);

        Assert.NotNull(sent);
        Assert.Equal(expectedDisplay, sent!.Display);
    }

    /// <summary>Builds an empty-lobby context whose sent controls are captured.</summary>
    /// <param name="capture">Invoked with each control the mode sends.</param>
    /// <returns>A context at the Unix epoch, with no participants.</returns>
    private static MatchContext BuildContext(Action<Control> capture) => new(
        now: DateTimeOffset.UnixEpoch,
        matchStartedAt: DateTimeOffset.UnixEpoch,
        startHp: 32,
        participants: [],
        scores: new Dictionary<int, int>(),
        addScore: (_, _) => { },
        send: capture);
}
