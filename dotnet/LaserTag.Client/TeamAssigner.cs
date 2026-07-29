namespace LaserTag.Client;

/// <summary>
/// Assigns a device's team by patching its persisted config over REST.
/// </summary>
/// <remarks>
/// <para>
/// Team is NOT part of the UDP control plane: a match reads each device's team
/// from its heartbeat, and the heartbeat reports the value persisted in NVS.
/// So assignment is a REST <c>PATCH /api/config {"ownTeam": n}</c> to the
/// device, addressed by the IP its heartbeat advertises.
/// </para>
/// <para>
/// Shaped like <see cref="FirmwareUpdater"/> — own short-lived
/// <see cref="HttpClient"/> per call, absolute URI from a heartbeat IP —
/// because both are "reach out to one board by address" operations rather than
/// part of the long-lived per-device client.
/// </para>
/// </remarks>
public sealed class TeamAssigner
{
    private readonly TimeSpan _timeout;

    /// <summary>Initializes a new instance of the <see cref="TeamAssigner"/> class.</summary>
    /// <param name="timeout">Per-request timeout. Defaults to 5 seconds.</param>
    public TeamAssigner(TimeSpan? timeout = null) =>
        _timeout = timeout ?? TimeSpan.FromSeconds(5);

    /// <summary>The outcome of one assignment.</summary>
    /// <param name="Ok">Whether the device accepted the new team.</param>
    /// <param name="Error">The failure reason when <paramref name="Ok"/> is false.</param>
    public readonly record struct Result(bool Ok, string? Error);

    /// <summary>
    /// Sets one device's team.
    /// </summary>
    /// <param name="ip">The device IP, as advertised in its heartbeat.</param>
    /// <param name="team">The team (0 = neutral, 1..4 = a side).</param>
    /// <param name="cancellationToken">A token to cancel the request.</param>
    /// <returns>The outcome; failures are returned, not thrown, so a caller
    /// assigning a whole fleet can report per-device results.</returns>
    public async Task<Result> AssignAsync(
        string ip,
        int team,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(ip))
        {
            return new Result(false, "device has no known IP (no heartbeat yet)");
        }

        using var http = new HttpClient { BaseAddress = new Uri($"http://{ip}"), Timeout = _timeout };
        var client = new LaserTagClient(http);
        try
        {
            await client
                .PatchConfigAsync(new Dictionary<string, object?> { ["ownTeam"] = team }, cancellationToken)
                .ConfigureAwait(false);
            return new Result(true, null);
        }
        catch (LaserTagApiException ex)
        {
            // Pre-2.2.0 firmware accepts any integer here, so a rejection means
            // either a real validation failure or an unreachable/odd device.
            return new Result(false, ex.Message);
        }
        catch (HttpRequestException ex)
        {
            return new Result(false, ex.Message);
        }
        catch (TaskCanceledException)
        {
            return new Result(false, "timed out");
        }
    }
}
