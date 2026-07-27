using LaserTag.Client;
using LaserTag.Client.Models;

namespace LaserTag.Client.Tests;

/// <summary>
/// Parser tests assert each golden vector from contract §1.5 parses to the exact
/// typed object, that every garbage/partial example drops to null without
/// throwing, and that <see cref="Control"/> formatting yields the exact strings.
/// </summary>
public sealed class UdpMessageParserTests
{
    private readonly UdpMessageParser _parser = new();

    [Fact]
    public void Parse_HeartbeatGoldenVector_ProducesExactHeartbeat()
    {
        const string line =
            "lasertag-matrix HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=2 mode=team-colours hp=100 online=1";

        UdpInboundMessage? result = _parser.Parse(line);

        var hb = Assert.IsType<Heartbeat>(result);
        Assert.Equal("lasertag-matrix", hb.Source);
        Assert.Equal("a1b2c3", hb.Id);
        Assert.Equal("192.168.1.24", hb.Ip);
        Assert.Equal("2.0.0", hb.Fw);
        Assert.Equal(2, hb.Team);
        Assert.Equal("team-colours", hb.Mode);
        Assert.Equal(100, hb.Hp);
        Assert.True(hb.Online);
    }

    [Fact]
    public void Parse_HitGoldenVector_ProducesExactHitEvent()
    {
        const string line =
            "lasertag-matrix EVT hit victim=a1b2c3 shooterTeam=2 dmg=2 proto=vatos hp=80 ts=12345";

        UdpInboundMessage? result = _parser.Parse(line);

        var hit = Assert.IsType<HitEvent>(result);
        Assert.Equal("lasertag-matrix", hit.Source);
        Assert.Equal("a1b2c3", hit.Victim);
        Assert.Equal(2, hit.ShooterTeam);
        Assert.Equal(2, hit.Dmg);
        Assert.Equal("vatos", hit.Proto);
        Assert.Equal(80, hit.Hp);
        Assert.Equal(12345L, hit.Ts);
    }

    [Fact]
    public void Parse_StateDeadGoldenVector_ProducesStateWithNullHp()
    {
        const string line = "lasertag-matrix EVT state s=dead ts=12500";

        UdpInboundMessage? result = _parser.Parse(line);

        var state = Assert.IsType<StateEvent>(result);
        Assert.Equal("lasertag-matrix", state.Source);
        Assert.Equal("dead", state.S);
        Assert.Null(state.Hp);
        Assert.Equal(12500L, state.Ts);
    }

    [Fact]
    public void Parse_StateRespawnGoldenVector_ProducesStateWithHp()
    {
        const string line = "lasertag-matrix EVT state s=respawn hp=100 ts=20000";

        UdpInboundMessage? result = _parser.Parse(line);

        var state = Assert.IsType<StateEvent>(result);
        Assert.Equal("lasertag-matrix", state.Source);
        Assert.Equal("respawn", state.S);
        Assert.Equal(100, state.Hp);
        Assert.Equal(20000L, state.Ts);
    }

    [Theory]
    [InlineData("lasertag-matrix EVT")]
    [InlineData("HB id=")]
    [InlineData("random noise 123")]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("EVT wat foo=bar")]
    [InlineData(null)]
    public void Parse_GarbageOrPartialLines_ReturnsNullWithoutThrowing(string? line)
    {
        UdpInboundMessage? result = _parser.Parse(line);
        Assert.Null(result);
    }

    [Fact]
    public void Parse_TrailingNewline_IsTolerated()
    {
        const string line =
            "lasertag-matrix HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=2 mode=team-colours hp=100 online=1\n";

        UdpInboundMessage? result = _parser.Parse(line);

        Assert.IsType<Heartbeat>(result);
    }

    [Fact]
    public void Parse_UnknownKeys_AreIgnoredNotRejected()
    {
        const string line =
            "lasertag-matrix HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=2 mode=team-colours hp=100 online=1 extra=foo future=99";

        UdpInboundMessage? result = _parser.Parse(line);

        var hb = Assert.IsType<Heartbeat>(result);
        Assert.Equal("a1b2c3", hb.Id);
        Assert.Equal(100, hb.Hp);
    }

    [Fact]
    public void Parse_HeartbeatMissingRequiredKey_ReturnsNull()
    {
        // Missing online= → incomplete heartbeat, must drop.
        const string line =
            "lasertag-matrix HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=2 mode=team-colours hp=100";

        Assert.Null(_parser.Parse(line));
    }

    [Fact]
    public void Parse_NonNumericIntField_ReturnsNull()
    {
        const string line =
            "lasertag-matrix HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=xx mode=team-colours hp=100 online=1";

        Assert.Null(_parser.Parse(line));
    }

    [Fact]
    public void Parse_HitTimestampNearUint32Max_FitsInLong()
    {
        // millis() can approach 2^32-1 (4294967295), which overflows int.
        const string line =
            "lasertag-matrix EVT hit victim=a1b2c3 shooterTeam=1 dmg=1 proto=vatos hp=0 ts=4294967295";

        UdpInboundMessage? result = _parser.Parse(line);

        var hit = Assert.IsType<HitEvent>(result);
        Assert.Equal(4294967295L, hit.Ts);
    }

    [Fact]
    public void FormatControl_StartWithTs_ProducesExactString()
    {
        var control = new Control { Kind = ControlKind.Start, Ts = 30000 };
        Assert.Equal("CTL start ts=30000", _parser.FormatControl(control));
    }

    [Fact]
    public void FormatControl_StartWithoutTs_OmitsTs()
    {
        var control = new Control { Kind = ControlKind.Start };
        Assert.Equal("CTL start", _parser.FormatControl(control));
    }

    [Fact]
    public void FormatControl_Stop_ProducesExactString()
    {
        var control = new Control { Kind = ControlKind.Stop };
        Assert.Equal("CTL stop", _parser.FormatControl(control));
    }

    [Fact]
    public void FormatControl_ResetWithHp_ProducesExactString()
    {
        var control = new Control { Kind = ControlKind.Reset, Hp = 100 };
        Assert.Equal("CTL reset hp=100", _parser.FormatControl(control));
    }

    [Fact]
    public void FormatControl_ResetWithoutHp_OmitsHp()
    {
        var control = new Control { Kind = ControlKind.Reset };
        Assert.Equal("CTL reset", _parser.FormatControl(control));
    }

    [Theory]
    [InlineData(ControlKind.Countdown, null, null, 5, null, null, "CTL countdown n=5")]
    [InlineData(ControlKind.GameOver, null, null, null, 2, null, "CTL gameover winner=2")]
    [InlineData(ControlKind.GameOver, null, null, null, 0, null, "CTL gameover winner=0")]
    [InlineData(ControlKind.Activate, null, null, null, null, "752b38", "CTL activate id=752b38")]
    [InlineData(ControlKind.Deactivate, null, null, null, null, null, "CTL deactivate")]
    [InlineData(ControlKind.Reset, null, 32, null, null, "752b38", "CTL reset hp=32 id=752b38")]
    [InlineData(ControlKind.Start, 30000L, null, null, null, "752b38", "CTL start ts=30000 id=752b38")]
    public void FormatControl_GrammarV2_EmitsGoldenStrings(
        ControlKind kind, long? ts, int? hp, int? n, int? winner, string? id, string expected)
    {
        var parser = new UdpMessageParser();
        var control = new Control { Kind = kind, Ts = ts, Hp = hp, N = n, Winner = winner, Id = id };
        Assert.Equal(expected, parser.FormatControl(control));
    }

    [Theory]
    [InlineData("CTL countdown n=5")]
    [InlineData("CTL gameover winner=0")]
    [InlineData("CTL activate id=752b38")]
    [InlineData("CTL deactivate")]
    [InlineData("CTL reset hp=32 id=752b38")]
    [InlineData("CTL start")]
    [InlineData("CTL stop")]
    public void ParseControl_RoundTripsFormattedStrings(string wire)
    {
        var parser = new UdpMessageParser();
        Control? parsed = parser.ParseControl(wire);
        Assert.NotNull(parsed);
        Assert.Equal(wire, parser.FormatControl(parsed));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("CTL")]
    [InlineData("CTL warp")]
    [InlineData("hostname CTL start")] // CTL lines carry no hostname prefix
    [InlineData("CTL countdown n=abc")]
    public void ParseControl_MalformedOrUnknown_ReturnsNull(string? wire)
    {
        Assert.Null(new UdpMessageParser().ParseControl(wire));
    }

    [Theory]
    [InlineData("CTL activate t=3200 id=eb20f8")]
    [InlineData("CTL chase on penalty=1 display=dark")]
    [InlineData("CTL chase off")]
    [InlineData("CTL score 1=4 2=0 3=12 4=7")]
    public void Control_RoundTrips_V21_Verbs(string wire)
    {
        var parser = new UdpMessageParser();
        Control? parsed = parser.ParseControl(wire);
        Assert.NotNull(parsed);
        Assert.Equal(wire, parser.FormatControl(parsed!));
    }

    [Fact]
    public void FormatControl_ChaseOn_EmitsPenaltyAndDisplay()
    {
        var parser = new UdpMessageParser();
        string wire = parser.FormatControl(new Control
        {
            Kind = ControlKind.ChaseOn, Penalty = 0, Display = "score",
        });
        Assert.Equal("CTL chase on penalty=0 display=score", wire);
    }

    [Fact]
    public void FormatControl_Score_OrdersTeamsAndPutsIdLast()
    {
        var parser = new UdpMessageParser();
        string wire = parser.FormatControl(new Control
        {
            Kind = ControlKind.Score,
            Scores = new Dictionary<int, int> { [2] = 9, [1] = 4 },
            Id = "eb20f8",
        });
        Assert.Equal("CTL score 1=4 2=9 3=0 4=0 id=eb20f8", wire);
    }

    [Fact]
    public void ParseHit_ReadsDormantFlag()
    {
        var parser = new UdpMessageParser();
        UdpInboundMessage? msg = parser.Parse(
            "lasertag-matrix3 EVT hit victim=eb20f8 shooterTeam=3 dmg=2 proto=vatos hp=32 ts=1234 dormant=1");
        HitEvent hit = Assert.IsType<HitEvent>(msg);
        Assert.True(hit.Dormant);
    }
}
