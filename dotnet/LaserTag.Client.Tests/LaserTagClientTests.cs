using System.Net;
using System.Text.Json;
using LaserTag.Client;
using LaserTag.Client.Models;

namespace LaserTag.Client.Tests;

/// <summary>
/// REST client tests use a stub handler returning the contract §2.2 golden JSON,
/// assert the client issues the correct method/path/body, deserializes responses
/// faithfully, and maps error responses (400 with error body, 404, 405, 500) to
/// <see cref="LaserTagApiException"/>.
/// </summary>
public sealed class LaserTagClientTests
{
    private static (LaserTagClient client, StubHttpMessageHandler handler) CreateClient(
        Func<HttpRequestMessage, string, HttpResponseMessage> responder)
    {
        var handler = new StubHttpMessageHandler(responder);
        var http = new HttpClient(handler) { BaseAddress = new Uri("http://lasertag-matrix") };
        return (new LaserTagClient(http), handler);
    }

    [Fact]
    public async Task GetStatusAsync_IssuesGetAndDeserializesGoldenStatus()
    {
        const string json = """
        {
          "deviceId": "a1b2c3",
          "hostname": "lasertag-matrix",
          "fw": "2.0.0",
          "mode": "team-colours",
          "ownTeam": 2,
          "hp": 100,
          "online": true,
          "uptimeMs": 123456,
          "rssi": -67
        }
        """;
        (LaserTagClient client, StubHttpMessageHandler handler) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(HttpStatusCode.OK, json));

        StatusDoc status = await client.GetStatusAsync();

        Assert.Equal(HttpMethod.Get, handler.LastRequest!.Method);
        Assert.Equal("/api/status", handler.LastRequest.RequestUri!.AbsolutePath);
        Assert.Equal("a1b2c3", status.DeviceId);
        Assert.Equal("lasertag-matrix", status.Hostname);
        Assert.Equal("2.0.0", status.Fw);
        Assert.Equal("team-colours", status.Mode);
        Assert.Equal(2, status.OwnTeam);
        Assert.Equal(100, status.Hp);
        Assert.True(status.Online);
        Assert.Equal(123456L, status.UptimeMs);
        Assert.Equal(-67, status.Rssi);
    }

    [Fact]
    public async Task GetConfigAsync_DeserializesGoldenConfigIncludingStringKeyedColours()
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
        (LaserTagClient client, StubHttpMessageHandler handler) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(HttpStatusCode.OK, json));

        ConfigDoc config = await client.GetConfigAsync();

        Assert.Equal(HttpMethod.Get, handler.LastRequest!.Method);
        Assert.Equal("/api/config", handler.LastRequest.RequestUri!.AbsolutePath);
        Assert.Equal("a1b2c3", config.DeviceId);
        Assert.Equal(new[] { 1, 2, 3, 4 }, config.EnabledTeams);
        Assert.Equal("vatos", config.ProtocolId);
        Assert.Equal(13, config.Brightness);

        // String JSON keys round-trip to int dictionary keys.
        Assert.Equal("#0000FF", config.TeamColours[1]);
        Assert.Equal("#FF0000", config.TeamColours[2]);
        Assert.Equal("#00FF00", config.TeamColours[3]);
        Assert.Equal("#FFFFFF", config.TeamColours[4]);
    }

    [Fact]
    public async Task PatchConfigAsync_SendsPatchWithExactPartialBodyAndReturnsFullConfig()
    {
        const string responseJson = """
        {
          "deviceId": "a1b2c3",
          "hostname": "lasertag-matrix",
          "ownTeam": 3,
          "enabledTeams": [1, 2, 3, 4],
          "protocolId": "vatos",
          "brightness": 20,
          "teamColours": { "1": "#0000FF", "2": "#FF0000", "3": "#00FF00", "4": "#FFFFFF" }
        }
        """;
        (LaserTagClient client, StubHttpMessageHandler handler) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(HttpStatusCode.OK, responseJson));

        var partial = new Dictionary<string, object?> { ["ownTeam"] = 3, ["brightness"] = 20 };
        ConfigDoc updated = await client.PatchConfigAsync(partial);

        Assert.Equal(HttpMethod.Patch, handler.LastRequest!.Method);
        Assert.Equal("/api/config", handler.LastRequest.RequestUri!.AbsolutePath);

        // The body carries exactly the two provided fields.
        using JsonDocument sent = JsonDocument.Parse(handler.LastRequestBody);
        Assert.Equal(2, sent.RootElement.EnumerateObject().Count());
        Assert.Equal(3, sent.RootElement.GetProperty("ownTeam").GetInt32());
        Assert.Equal(20, sent.RootElement.GetProperty("brightness").GetInt32());

        Assert.Equal(3, updated.OwnTeam);
        Assert.Equal(20, updated.Brightness);
    }

    [Fact]
    public async Task PatchConfigAsync_UnknownField_Surfaces400Error()
    {
        const string errorJson = """{ "error": "unknown field: foo" }""";
        (LaserTagClient client, _) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(HttpStatusCode.BadRequest, errorJson));

        var partial = new Dictionary<string, object?> { ["foo"] = 1 };
        LaserTagApiException ex =
            await Assert.ThrowsAsync<LaserTagApiException>(() => client.PatchConfigAsync(partial));

        Assert.Equal(HttpStatusCode.BadRequest, ex.StatusCode);
        Assert.Equal("unknown field: foo", ex.ErrorMessage);
        Assert.Contains("unknown field: foo", ex.Message);
    }

    [Fact]
    public async Task PatchConfigAsync_UnknownFieldBody_SerializesExactly()
    {
        (LaserTagClient client, StubHttpMessageHandler handler) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(
                HttpStatusCode.BadRequest, """{ "error": "unknown field: foo" }"""));

        var partial = new Dictionary<string, object?> { ["foo"] = 1 };
        await Assert.ThrowsAsync<LaserTagApiException>(() => client.PatchConfigAsync(partial));

        using JsonDocument sent = JsonDocument.Parse(handler.LastRequestBody);
        Assert.Equal(1, sent.RootElement.GetProperty("foo").GetInt32());
        Assert.Single(sent.RootElement.EnumerateObject());
    }

    [Fact]
    public async Task SetModeAsync_PostsModeDocAndEchoesItBack()
    {
        const string json = """
        { "mode": "team-colours", "timings": { "darkMinMs": 5000, "darkMaxMs": 15000 } }
        """;
        (LaserTagClient client, StubHttpMessageHandler handler) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(HttpStatusCode.OK, json));

        var mode = new ModeDoc
        {
            Mode = "team-colours",
            Timings = new ModeTimings { DarkMinMs = 5000, DarkMaxMs = 15000 },
        };
        ModeDoc echoed = await client.SetModeAsync(mode);

        Assert.Equal(HttpMethod.Post, handler.LastRequest!.Method);
        Assert.Equal("/api/mode", handler.LastRequest.RequestUri!.AbsolutePath);

        using JsonDocument sent = JsonDocument.Parse(handler.LastRequestBody);
        Assert.Equal("team-colours", sent.RootElement.GetProperty("mode").GetString());
        JsonElement timings = sent.RootElement.GetProperty("timings");
        Assert.Equal(5000, timings.GetProperty("darkMinMs").GetInt32());
        Assert.Equal(15000, timings.GetProperty("darkMaxMs").GetInt32());

        Assert.Equal("team-colours", echoed.Mode);
        Assert.Equal(5000, echoed.Timings!.DarkMinMs);
        Assert.Equal(15000, echoed.Timings.DarkMaxMs);
    }

    [Theory]
    [InlineData("identify", null, null, null, """{"cmd":"identify"}""")]
    [InlineData("bright", 20, null, null, """{"cmd":"bright","value":20}""")]
    [InlineData("debug", 1, null, null, """{"cmd":"debug","value":1}""")]
    public async Task SendCommandAsync_SerializesOptionalFieldsOnlyWhenPresent(
        string cmd, int? value, int? team, int? damage, string expectedBody)
    {
        (LaserTagClient client, StubHttpMessageHandler handler) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(HttpStatusCode.OK, """{"ok":true}"""));

        var command = new CommandDoc { Cmd = cmd, Value = value, Team = team, Damage = damage };
        bool ok = await client.SendCommandAsync(command);

        Assert.True(ok);
        Assert.Equal(HttpMethod.Post, handler.LastRequest!.Method);
        Assert.Equal("/api/command", handler.LastRequest.RequestUri!.AbsolutePath);

        // Compare as parsed JSON to be independent of property ordering.
        using JsonDocument expected = JsonDocument.Parse(expectedBody);
        using JsonDocument actual = JsonDocument.Parse(handler.LastRequestBody);
        Assert.Equal(
            expected.RootElement.EnumerateObject().Count(),
            actual.RootElement.EnumerateObject().Count());
        foreach (JsonProperty prop in expected.RootElement.EnumerateObject())
        {
            Assert.True(actual.RootElement.TryGetProperty(prop.Name, out JsonElement actualValue));
            Assert.Equal(prop.Value.ToString(), actualValue.ToString());
        }
    }

    [Fact]
    public async Task SendCommandAsync_HitCommand_SerializesTeamAndDamage()
    {
        (LaserTagClient client, StubHttpMessageHandler handler) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(HttpStatusCode.OK, """{"ok":true}"""));

        var command = new CommandDoc { Cmd = "hit", Team = 2, Damage = 2 };
        await client.SendCommandAsync(command);

        using JsonDocument sent = JsonDocument.Parse(handler.LastRequestBody);
        Assert.Equal("hit", sent.RootElement.GetProperty("cmd").GetString());
        Assert.Equal(2, sent.RootElement.GetProperty("team").GetInt32());
        Assert.Equal(2, sent.RootElement.GetProperty("damage").GetInt32());
        Assert.False(sent.RootElement.TryGetProperty("value", out _));
    }

    [Fact]
    public async Task GetStatusAsync_NotFound_ThrowsWith404()
    {
        (LaserTagClient client, _) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(
                HttpStatusCode.NotFound, """{ "error": "no such route" }"""));

        LaserTagApiException ex =
            await Assert.ThrowsAsync<LaserTagApiException>(() => client.GetStatusAsync());

        Assert.Equal(HttpStatusCode.NotFound, ex.StatusCode);
        Assert.Equal("no such route", ex.ErrorMessage);
    }

    [Fact]
    public async Task SetModeAsync_MethodNotAllowed_ThrowsWith405()
    {
        (LaserTagClient client, _) =
            CreateClient((_, _) => new HttpResponseMessage(HttpStatusCode.MethodNotAllowed));

        LaserTagApiException ex =
            await Assert.ThrowsAsync<LaserTagApiException>(
                () => client.SetModeAsync(new ModeDoc { Mode = "idle" }));

        Assert.Equal(HttpStatusCode.MethodNotAllowed, ex.StatusCode);
    }

    [Fact]
    public async Task PatchConfigAsync_NvsWriteFailure_ThrowsWith500()
    {
        (LaserTagClient client, _) =
            CreateClient((_, _) => StubHttpMessageHandler.Json(
                HttpStatusCode.InternalServerError, """{ "error": "nvs write failed" }"""));

        LaserTagApiException ex = await Assert.ThrowsAsync<LaserTagApiException>(
            () => client.PatchConfigAsync(new Dictionary<string, object?> { ["brightness"] = 5 }));

        Assert.Equal(HttpStatusCode.InternalServerError, ex.StatusCode);
        Assert.Equal("nvs write failed", ex.ErrorMessage);
    }

    [Fact]
    public async Task NonJsonErrorBody_DoesNotThrowDuringErrorMapping()
    {
        (LaserTagClient client, _) =
            CreateClient((_, _) => new HttpResponseMessage(HttpStatusCode.NotFound)
            {
                Content = new StringContent("<html>404</html>", System.Text.Encoding.UTF8, "text/html"),
            });

        LaserTagApiException ex =
            await Assert.ThrowsAsync<LaserTagApiException>(() => client.GetStatusAsync());

        Assert.Equal(HttpStatusCode.NotFound, ex.StatusCode);
        Assert.Null(ex.ErrorMessage);
        Assert.Contains("404", ex.RawBody);
    }
}
