using System.Text.Json;

namespace LaserTag.Client;

/// <summary>
/// Pushes a firmware image to a device's <c>POST /api/update</c> endpoint
/// (fleet-ota spec). Multipart form-data, because the ESP32 WebServer's
/// upload machinery only parses multipart bodies. Reusable from any .NET
/// client (host CLI today, the Android app later).
/// </summary>
/// <param name="timeout">
/// Per-upload timeout. Flash writes take 10–30 s on the device; default 90 s.
/// </param>
public sealed class FirmwareUpdater(TimeSpan? timeout = null)
{
    private readonly HttpClient _http = new() { Timeout = timeout ?? TimeSpan.FromSeconds(90) };

    /// <summary>Outcome of one device upload.</summary>
    /// <param name="Ok">Whether the device accepted and committed the image.</param>
    /// <param name="DeviceVersion">The version the device reported (its running
    /// version at response time), when provided.</param>
    /// <param name="Error">Failure detail when <paramref name="Ok"/> is false.</param>
    public sealed record Result(bool Ok, string? DeviceVersion, string? Error);

    /// <summary>Uploads a firmware image to one device.</summary>
    /// <param name="host">Device address — hostname, IP, or host:port.</param>
    /// <param name="binPath">Path to the firmware .bin.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The upload outcome; never throws for transport failures.</returns>
    public async Task<Result> UploadAsync(string host, string binPath, CancellationToken ct)
    {
        try
        {
            await using FileStream file = File.OpenRead(binPath);
            using var content = new MultipartFormDataContent();
            using var fw = new StreamContent(file);
            fw.Headers.ContentType = new("application/octet-stream");
            content.Add(fw, "fw", Path.GetFileName(binPath));

            using HttpResponseMessage resp =
                await _http.PostAsync($"http://{host}/api/update", content, ct);
            string body = await resp.Content.ReadAsStringAsync(ct);
            if (!resp.IsSuccessStatusCode)
            {
                return new Result(false, null, ExtractField(body, "error") ?? $"HTTP {(int)resp.StatusCode}");
            }

            return new Result(true, ExtractField(body, "version"), null);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or IOException)
        {
            return new Result(false, null, ex.Message);
        }
    }

    private static string? ExtractField(string json, string field)
    {
        try
        {
            using JsonDocument doc = JsonDocument.Parse(json);
            return doc.RootElement.TryGetProperty(field, out JsonElement v) ? v.GetString() : null;
        }
        catch (JsonException)
        {
            return null;
        }
    }
}
