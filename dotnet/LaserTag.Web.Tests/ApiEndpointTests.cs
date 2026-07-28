using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

namespace LaserTag.Web.Tests;

/// <summary>
/// Exercises the JSON API endpoints in <c>Program.cs</c> against a real,
/// in-process host — a fresh lobby with no hardware attached.
/// </summary>
public class ApiEndpointTests : IClassFixture<LaserTagWebFactory>
{
    private readonly HttpClient _client;

    public ApiEndpointTests(LaserTagWebFactory factory) => _client = factory.CreateClient();

    [Fact]
    public async Task GetDevices_OnFreshHost_ReturnsEmptyArray()
    {
        HttpResponseMessage response = await _client.GetAsync("/api/devices");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        JsonElement body = await response.Content.ReadFromJsonAsync<JsonElement>();
        Assert.Equal(JsonValueKind.Array, body.ValueKind);
        Assert.Empty(body.EnumerateArray());
    }

    [Fact]
    public async Task GetMatch_OnFreshHost_ReturnsLobbyPhase()
    {
        HttpResponseMessage response = await _client.GetAsync("/api/match");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        // Minimal API JSON serialization defaults to camelCase property names.
        JsonElement body = await response.Content.ReadFromJsonAsync<JsonElement>();
        Assert.Equal("Lobby", body.GetProperty("phase").GetString());
    }

    [Fact]
    public async Task PostMatchStart_WithNoOnlineDevices_ReturnsBadRequest_MentioningNoDevices()
    {
        // Guards the empty-lobby refusal: there is no hardware in tests, so a
        // valid mode request must still be turned away by GameService, not
        // silently accepted.
        HttpResponseMessage response = await _client.PostAsJsonAsync(
            "/api/match/start", new { mode = "dm", duration = "5m" });

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        JsonElement body = await response.Content.ReadFromJsonAsync<JsonElement>();
        Assert.Contains("online devices", body.GetProperty("error").GetString(), StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task PostMatchStart_WithUnknownMode_ReturnsBadRequest_NamingValidModes()
    {
        HttpResponseMessage response = await _client.PostAsJsonAsync(
            "/api/match/start", new { mode = "nonsense" });

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        JsonElement body = await response.Content.ReadFromJsonAsync<JsonElement>();
        string error = body.GetProperty("error").GetString()!;
        Assert.Contains("dm", error, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("elim", error, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("chase", error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task PostMatchStart_ChaseWithNoEndCondition_ReturnsBadRequest()
    {
        // A chase match with neither duration nor firstTo would never end;
        // ModeFactory must reject it before it ever reaches the engine.
        HttpResponseMessage response = await _client.PostAsJsonAsync(
            "/api/match/start", new { mode = "chase" });

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }

    [Fact]
    public async Task PostMatchStop_ReturnsOk()
    {
        HttpResponseMessage response = await _client.PostAsync("/api/match/stop", content: null);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public async Task PostControl_WithResetKind_ReturnsOk()
    {
        HttpResponseMessage response = await _client.PostAsJsonAsync("/api/control", new { kind = "reset" });

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public async Task PostControl_WithBogusKind_ReturnsBadRequest()
    {
        HttpResponseMessage response = await _client.PostAsJsonAsync("/api/control", new { kind = "bogus" });

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }
}
