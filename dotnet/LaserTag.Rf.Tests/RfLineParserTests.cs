namespace LaserTag.Rf.Tests;

public class RfLineParserTests
{
    [Fact]
    public void TryParse_ValidRfLine_ReturnsCapture()
    {
        bool ok = RfLineParser.TryParse(
            "RF ch=76 rate=1m ts=1234567 n=4 data=AABBCCDD", out RfCapture capture);

        Assert.True(ok);
        Assert.Equal(76, capture.Channel);
        Assert.Equal("1m", capture.Rate);
        Assert.Equal(1234567, capture.TimestampUs);
        Assert.Equal(new byte[] { 0xAA, 0xBB, 0xCC, 0xDD }, capture.Data);
    }

    [Theory]
    [InlineData("SCAN ch=76 mhz=2476 hits=3")]
    [InlineData("RF ch=76 rate=1m ts=1 n=4 data=ZZZZ")]
    [InlineData("RF ch=76 rate=1m ts=1 n=4")]
    [InlineData("")]
    public void TryParse_NonCaptureLines_ReturnsFalse(string line)
    {
        Assert.False(RfLineParser.TryParse(line, out _));
    }

    [Fact]
    public void TryParse_OddLengthHex_ReturnsFalse()
    {
        // A truncated line from a mid-transmission serial connect must be
        // rejected rather than silently half-decoded.
        Assert.False(RfLineParser.TryParse("RF ch=1 rate=1m ts=1 n=2 data=ABC", out _));
    }
}
