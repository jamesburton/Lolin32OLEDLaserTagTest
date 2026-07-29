using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace LaserTag.Client;

/// <summary>One entry in a device's storage directory listing.</summary>
public sealed class FileEntry
{
    /// <summary>Gets the entry name (leaf only, not a full path).</summary>
    [JsonPropertyName("name")]
    public required string Name { get; init; }

    /// <summary>Gets the size in bytes; 0 for a directory.</summary>
    [JsonPropertyName("size")]
    public int Size { get; init; }

    /// <summary>Gets a value indicating whether the entry is a directory.</summary>
    [JsonPropertyName("dir")]
    public bool IsDirectory { get; init; }
}

/// <summary>A device's storage status and one directory's contents.</summary>
public sealed class StorageListing
{
    /// <summary>Gets a value indicating whether a card is mounted.</summary>
    [JsonPropertyName("present")]
    public bool Present { get; init; }

    /// <summary>Gets the card's total size in kB.</summary>
    [JsonPropertyName("totalKb")]
    public long TotalKb { get; init; }

    /// <summary>Gets the used space in kB.</summary>
    [JsonPropertyName("usedKb")]
    public long UsedKb { get; init; }

    /// <summary>Gets the directory that was listed.</summary>
    [JsonPropertyName("path")]
    public string Path { get; init; } = "/";

    /// <summary>Gets the entries directly under <see cref="Path"/>.</summary>
    [JsonPropertyName("files")]
    public IReadOnlyList<FileEntry> Files { get; init; } = [];
}

/// <summary>
/// Manages a single board's clip storage over its REST surface: list, upload,
/// download and delete.
/// </summary>
/// <remarks>
/// <para>
/// Separate from <see cref="LaserTagClient"/> because these calls are
/// file-shaped rather than document-shaped — uploads stream, downloads return
/// bytes, and failures are returned rather than thrown so a caller syncing a
/// whole folder can report per-file results.
/// </para>
/// <para>
/// Storage is the board's on-board flash partition, not the microSD — it needs
/// no socket, module or wiring. Requires device firmware 2.4.0 or newer;
/// earlier images have no <c>/api/files</c> and answer 404.
/// </para>
/// </remarks>
public sealed class DeviceFilesClient
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private readonly TimeSpan _timeout;

    /// <summary>Initializes a new instance of the <see cref="DeviceFilesClient"/> class.</summary>
    /// <param name="timeout">Per-request timeout. Defaults to 30 seconds, which
    /// accommodates a multi-hundred-kB clip upload over a weak link.</param>
    public DeviceFilesClient(TimeSpan? timeout = null) =>
        _timeout = timeout ?? TimeSpan.FromSeconds(30);

    /// <summary>The outcome of an SD operation.</summary>
    /// <typeparam name="T">The payload type.</typeparam>
    /// <param name="Ok">Whether the operation succeeded.</param>
    /// <param name="Value">The payload on success.</param>
    /// <param name="Error">The failure reason otherwise.</param>
    public readonly record struct Result<T>(bool Ok, T? Value, string? Error);

    /// <summary>Lists a directory in a board's storage.</summary>
    /// <param name="ip">The device IP.</param>
    /// <param name="path">The directory to list. Defaults to the card root.</param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>The listing, or a failure.</returns>
    public async Task<Result<StorageListing>> ListAsync(
        string ip, string path = "/", CancellationToken cancellationToken = default)
    {
        return await SendAsync<StorageListing>(
            ip,
            http => http.GetAsync($"/api/files?path={Uri.EscapeDataString(path)}", cancellationToken),
            cancellationToken).ConfigureAwait(false);
    }

    /// <summary>Uploads a local file to a board's card.</summary>
    /// <param name="ip">The device IP.</param>
    /// <param name="localPath">The local file to send.</param>
    /// <param name="remotePath">The destination path on the card, e.g. <c>/sfx/quack.wav</c>.</param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>Success, or the failure reason.</returns>
    public async Task<Result<bool>> UploadAsync(
        string ip, string localPath, string remotePath, CancellationToken cancellationToken = default)
    {
        if (!File.Exists(localPath))
        {
            return new Result<bool>(false, false, $"local file not found: {localPath}");
        }

        byte[] bytes;
        try
        {
            bytes = await File.ReadAllBytesAsync(localPath, cancellationToken).ConfigureAwait(false);
        }
        catch (IOException ex)
        {
            return new Result<bool>(false, false, ex.Message);
        }

        Result<UploadAck> result = await SendAsync<UploadAck>(
            ip,
            http =>
            {
                // Multipart because the device's streamed upload handler is the
                // ESP32 WebServer's HTTPUpload path, which only fires for
                // multipart/form-data bodies — the same shape /api/update uses.
                var content = new MultipartFormDataContent();
                var file = new ByteArrayContent(bytes);
                file.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
                content.Add(file, "file", Path.GetFileName(remotePath));
                return http.PostAsync(
                    $"/api/files/file?path={Uri.EscapeDataString(remotePath)}", content, cancellationToken);
            },
            cancellationToken).ConfigureAwait(false);

        if (!result.Ok)
        {
            return new Result<bool>(false, false, result.Error);
        }

        // The device reports what it actually stored; a size mismatch means a
        // truncated write that would only surface later as a corrupt clip.
        int stored = result.Value?.Size ?? -1;
        return stored == bytes.Length
            ? new Result<bool>(true, true, null)
            : new Result<bool>(false, false, $"size mismatch: sent {bytes.Length} bytes, device stored {stored}");
    }

    /// <summary>Downloads a file from a board's card.</summary>
    /// <param name="ip">The device IP.</param>
    /// <param name="remotePath">The path on the card.</param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>The file bytes, or a failure.</returns>
    public async Task<Result<byte[]>> DownloadAsync(
        string ip, string remotePath, CancellationToken cancellationToken = default)
    {
        using var http = Create(ip);
        try
        {
            using HttpResponseMessage response = await http
                .GetAsync($"/api/files/file?path={Uri.EscapeDataString(remotePath)}", cancellationToken)
                .ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                return new Result<byte[]>(false, null, await DescribeAsync(response, cancellationToken).ConfigureAwait(false));
            }

            byte[] bytes = await response.Content.ReadAsByteArrayAsync(cancellationToken).ConfigureAwait(false);
            return new Result<byte[]>(true, bytes, null);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException)
        {
            return new Result<byte[]>(false, null, ex.Message);
        }
    }

    /// <summary>Deletes a file from a board's card.</summary>
    /// <param name="ip">The device IP.</param>
    /// <param name="remotePath">The path on the card.</param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>Success, or the failure reason.</returns>
    public async Task<Result<bool>> DeleteAsync(
        string ip, string remotePath, CancellationToken cancellationToken = default)
    {
        Result<OkAck> result = await SendAsync<OkAck>(
            ip,
            http => http.DeleteAsync($"/api/files/file?path={Uri.EscapeDataString(remotePath)}", cancellationToken),
            cancellationToken).ConfigureAwait(false);
        return new Result<bool>(result.Ok, result.Ok, result.Error);
    }

    private HttpClient Create(string ip) =>
        new() { BaseAddress = new Uri($"http://{ip}"), Timeout = _timeout };

    private async Task<Result<T>> SendAsync<T>(
        string ip,
        Func<HttpClient, Task<HttpResponseMessage>> send,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(ip))
        {
            return new Result<T>(false, default, "device has no known IP (no heartbeat yet)");
        }

        using HttpClient http = Create(ip);
        try
        {
            using HttpResponseMessage response = await send(http).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                return new Result<T>(false, default, await DescribeAsync(response, cancellationToken).ConfigureAwait(false));
            }

            string body = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            T? value = JsonSerializer.Deserialize<T>(body, JsonOptions);
            return new Result<T>(true, value, null);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException)
        {
            return new Result<T>(false, default, ex.Message);
        }
    }

    private static async Task<string> DescribeAsync(HttpResponseMessage response, CancellationToken cancellationToken)
    {
        string body = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        if (response.StatusCode == HttpStatusCode.NotFound && !body.Contains("error", StringComparison.OrdinalIgnoreCase))
        {
            // A bare 404 with no device error body means the route itself is
            // absent, i.e. firmware older than 2.3.0.
            return "no /api/files on this device — firmware 2.4.0+ required";
        }

        try
        {
            using JsonDocument doc = JsonDocument.Parse(body);
            if (doc.RootElement.TryGetProperty("error", out JsonElement error))
            {
                return $"HTTP {(int)response.StatusCode}: {error.GetString()}";
            }
        }
        catch (JsonException)
        {
            // Non-JSON error body; fall through to the raw text.
        }

        return $"HTTP {(int)response.StatusCode}: {body}";
    }

    private sealed class UploadAck
    {
        [JsonPropertyName("size")]
        public int Size { get; init; }
    }

    private sealed class OkAck
    {
        [JsonPropertyName("ok")]
        public bool Ok { get; init; }
    }
}
