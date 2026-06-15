using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using LaserTag.Client.Models;

namespace LaserTag.Client;

/// <summary>
/// A typed, unit-testable client for a single device's REST surface
/// (contract §2). Wraps an injected <see cref="HttpClient"/> so the transport
/// (and thus tests) can substitute a stub <see cref="HttpMessageHandler"/>.
/// </summary>
/// <remarks>
/// Non-success responses are mapped to <see cref="LaserTagApiException"/>, which
/// surfaces the status code and any device <c>{"error": "..."}</c> body
/// (design §8). The client never mutates the supplied <see cref="HttpClient"/>'s
/// configuration beyond issuing requests; set the <c>BaseAddress</c> on the
/// injected client (e.g. <c>http://lasertag-matrix/</c>).
/// </remarks>
public sealed class LaserTagClient
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private readonly HttpClient _http;

    /// <summary>
    /// Initializes a new instance of the <see cref="LaserTagClient"/> class.
    /// </summary>
    /// <param name="httpClient">
    /// The HTTP client used for all requests. Its <c>BaseAddress</c> should point
    /// at the target device.
    /// </param>
    /// <exception cref="ArgumentNullException">Thrown if <paramref name="httpClient"/> is null.</exception>
    public LaserTagClient(HttpClient httpClient)
    {
        ArgumentNullException.ThrowIfNull(httpClient);
        _http = httpClient;
    }

    /// <summary>
    /// Gets the device's live runtime status (<c>GET /api/status</c>).
    /// </summary>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>The deserialized <see cref="StatusDoc"/>.</returns>
    /// <exception cref="LaserTagApiException">Thrown on a non-success response.</exception>
    public async Task<StatusDoc> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, "/api/status");
        return await SendAsync<StatusDoc>(request, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>
    /// Gets the device's full persisted configuration (<c>GET /api/config</c>).
    /// </summary>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>The deserialized <see cref="ConfigDoc"/>.</returns>
    /// <exception cref="LaserTagApiException">Thrown on a non-success response.</exception>
    public async Task<ConfigDoc> GetConfigAsync(CancellationToken cancellationToken = default)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, "/api/config");
        return await SendAsync<ConfigDoc>(request, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>
    /// Applies a partial configuration update (<c>PATCH /api/config</c>). Only
    /// the supplied keys are changed; the full updated <see cref="ConfigDoc"/> is
    /// returned. Unknown keys are rejected by the device with <c>400</c>.
    /// </summary>
    /// <param name="partial">
    /// The fields to change, keyed by their JSON config field names (e.g.
    /// <c>ownTeam</c>, <c>brightness</c>). A dictionary is used rather than a
    /// strongly-typed document so that arbitrary partial bodies — and the
    /// unknown-field <c>400</c> path — can be expressed exactly.
    /// </param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>The deserialized full <see cref="ConfigDoc"/> after the update.</returns>
    /// <exception cref="LaserTagApiException">
    /// Thrown on a non-success response; for an unknown field the device returns
    /// <c>400</c> and the <c>error</c> message is surfaced.
    /// </exception>
    public async Task<ConfigDoc> PatchConfigAsync(
        IReadOnlyDictionary<string, object?> partial,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(partial);
        using var request = new HttpRequestMessage(HttpMethod.Patch, "/api/config")
        {
            Content = JsonContent.Create(partial, options: JsonOptions),
        };
        return await SendAsync<ConfigDoc>(request, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>
    /// Sets the runtime mode and timings (<c>POST /api/mode</c>). Not persisted;
    /// the device echoes the document back.
    /// </summary>
    /// <param name="mode">The mode document to apply.</param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>The echoed <see cref="ModeDoc"/>.</returns>
    /// <exception cref="LaserTagApiException">Thrown on a non-success response.</exception>
    public async Task<ModeDoc> SetModeAsync(ModeDoc mode, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(mode);
        using var request = new HttpRequestMessage(HttpMethod.Post, "/api/mode")
        {
            Content = JsonContent.Create(mode, options: JsonOptions),
        };
        return await SendAsync<ModeDoc>(request, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>
    /// Sends a structured one-shot command (<c>POST /api/command</c>).
    /// </summary>
    /// <param name="command">The command to send.</param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>
    /// <see langword="true"/> when the device acknowledges with <c>{"ok":true}</c>.
    /// </returns>
    /// <exception cref="LaserTagApiException">Thrown on a non-success response.</exception>
    public async Task<bool> SendCommandAsync(CommandDoc command, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(command);
        using var request = new HttpRequestMessage(HttpMethod.Post, "/api/command")
        {
            Content = JsonContent.Create(command, options: JsonOptions),
        };
        CommandAck ack = await SendAsync<CommandAck>(request, cancellationToken).ConfigureAwait(false);
        return ack.Ok;
    }

    private async Task<T> SendAsync<T>(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        using HttpResponseMessage response =
            await _http.SendAsync(request, cancellationToken).ConfigureAwait(false);

        if (!response.IsSuccessStatusCode)
        {
            await ThrowApiExceptionAsync(response, cancellationToken).ConfigureAwait(false);
        }

        T? result = await response.Content
            .ReadFromJsonAsync<T>(JsonOptions, cancellationToken)
            .ConfigureAwait(false);

        if (result is null)
        {
            throw new LaserTagApiException(
                response.StatusCode,
                "Response body was empty or null.",
                rawBody: null);
        }

        return result;
    }

    private static async Task ThrowApiExceptionAsync(
        HttpResponseMessage response,
        CancellationToken cancellationToken)
    {
        string? body = null;
        string? errorMessage = null;

        try
        {
            body = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            if (!string.IsNullOrWhiteSpace(body))
            {
                using JsonDocument doc = JsonDocument.Parse(body);
                if (doc.RootElement.ValueKind == JsonValueKind.Object &&
                    doc.RootElement.TryGetProperty("error", out JsonElement errorElement) &&
                    errorElement.ValueKind == JsonValueKind.String)
                {
                    errorMessage = errorElement.GetString();
                }
            }
        }
        catch (JsonException)
        {
            // Non-JSON error body (e.g. a plain 404/405 page) — keep the raw body
            // for diagnostics but surface no parsed error message.
        }

        throw new LaserTagApiException(response.StatusCode, errorMessage, body);
    }

    /// <summary>Acknowledgement shape returned by <c>POST /api/command</c>.</summary>
    private sealed record CommandAck
    {
        public bool Ok { get; init; }
    }
}
