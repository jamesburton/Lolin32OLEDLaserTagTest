namespace LaserTag.Game.Tests;

public class DurationParserTests
{
    [Theory]
    [InlineData("5m", 300)]
    [InlineData("90s", 90)]
    [InlineData("1h", 3600)]
    [InlineData("300", 300)]
    public void TryParse_ValidInputs(string text, int expectedSeconds)
    {
        Assert.True(DurationParser.TryParse(text, out TimeSpan value));
        Assert.Equal(TimeSpan.FromSeconds(expectedSeconds), value);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("0m")]
    [InlineData("-5m")]
    [InlineData("5x")]
    [InlineData("m")]
    [InlineData("five")]
    [InlineData("NaN")]
    [InlineData("Infinity")]
    [InlineData("-Infinity")]
    [InlineData("1e300s")]
    [InlineData("9999999999999999999h")]
    public void TryParse_InvalidInputs_ReturnFalse(string? text)
    {
        Assert.False(DurationParser.TryParse(text, out _));
    }
}
