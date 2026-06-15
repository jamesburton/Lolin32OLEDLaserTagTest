using System.Text.Json;
using LaserTag.Client.Models;

namespace LaserTag.Client.Tests;

/// <summary>
/// Direct System.Text.Json round-trip tests for the contract §2.2 JSON shapes,
/// focusing on the string-keyed <c>teamColours</c> dictionary and exact property
/// names.
/// </summary>
public sealed class ModelSerializationTests
{
    private static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web);

    [Fact]
    public void ConfigDoc_DeserializesStringKeyedTeamColours()
    {
        const string json = """
        {
          "deviceId": "a1b2c3",
          "hostname": "lasertag-matrix",
          "ownTeam": 2,
          "enabledTeams": [1, 2, 3, 4],
          "protocolId": "vatos",
          "brightness": 13,
          "teamColours": { "1": "#0000FF", "2": "#FF0000", "3": "#00FF00", "4": "#FFFFFF" }
        }
        """;

        ConfigDoc? config = JsonDeserialize<ConfigDoc>(json);

        Assert.NotNull(config);
        Assert.Equal(4, config!.TeamColours.Count);
        Assert.Equal("#0000FF", config.TeamColours[1]);
        Assert.Equal("#FFFFFF", config.TeamColours[4]);
    }

    [Fact]
    public void ConfigDoc_RoundTrips_WithStringDictionaryKeys()
    {
        var config = new ConfigDoc
        {
            DeviceId = "a1b2c3",
            Hostname = "lasertag-matrix",
            OwnTeam = 2,
            EnabledTeams = [1, 2, 3, 4],
            ProtocolId = "vatos",
            Brightness = 13,
            TeamColours = new Dictionary<int, string>
            {
                [1] = "#0000FF",
                [2] = "#FF0000",
            },
        };

        string json = JsonSerializer.Serialize(config, Options);

        using JsonDocument doc = JsonDocument.Parse(json);
        JsonElement colours = doc.RootElement.GetProperty("teamColours");
        // Dictionary<int,string> serializes keys as JSON strings.
        Assert.Equal("#0000FF", colours.GetProperty("1").GetString());
        Assert.Equal("#FF0000", colours.GetProperty("2").GetString());

        ConfigDoc? back = JsonDeserialize<ConfigDoc>(json);
        Assert.Equal("#0000FF", back!.TeamColours[1]);
    }

    [Fact]
    public void StatusDoc_UsesExactJsonPropertyNames()
    {
        var status = new StatusDoc
        {
            DeviceId = "a1b2c3",
            Hostname = "lasertag-matrix",
            Fw = "2.0.0",
            Mode = "team-colours",
            OwnTeam = 2,
            Hp = 100,
            Online = true,
            UptimeMs = 123456,
            Rssi = -67,
        };

        string json = JsonSerializer.Serialize(status, Options);
        using JsonDocument doc = JsonDocument.Parse(json);

        Assert.True(doc.RootElement.TryGetProperty("deviceId", out _));
        Assert.True(doc.RootElement.TryGetProperty("ownTeam", out _));
        Assert.True(doc.RootElement.TryGetProperty("uptimeMs", out _));
        Assert.True(doc.RootElement.TryGetProperty("rssi", out _));
    }

    [Fact]
    public void CommandDoc_OmitsNullOptionalFields()
    {
        var command = new CommandDoc { Cmd = "identify" };
        string json = JsonSerializer.Serialize(command, Options);

        using JsonDocument doc = JsonDocument.Parse(json);
        Assert.Equal("identify", doc.RootElement.GetProperty("cmd").GetString());
        Assert.False(doc.RootElement.TryGetProperty("value", out _));
        Assert.False(doc.RootElement.TryGetProperty("team", out _));
        Assert.False(doc.RootElement.TryGetProperty("damage", out _));
    }

    private static T? JsonDeserialize<T>(string json) => JsonSerializer.Deserialize<T>(json, Options);
}
