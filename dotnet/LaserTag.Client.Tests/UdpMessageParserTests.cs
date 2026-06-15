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
}
