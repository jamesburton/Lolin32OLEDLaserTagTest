using LaserTag.Client;
using LaserTag.Client.Models;
using LaserTag.Game;

namespace LaserTag.Ui;

/// <summary>
/// Everything a manager UI needs from "the thing running the match".
/// </summary>
/// <remarks>
/// Both shells back this with a local <c>GameService</c> — the web manager runs
/// the engine on the PC, the Android app runs it on the phone — so the screens
/// are written once and rendered by both. The interface exists so components
/// stay testable without a UDP socket, and so a future thin client (a phone
/// driving a remote host over HTTP) can slot in without the screens changing.
/// </remarks>
public interface IGameSession
{
    /// <summary>The current device roster.</summary>
    /// <returns>One entry per device seen since start-up.</returns>
    IReadOnlyList<RosterEntry> Devices();

    /// <summary>The current match state.</summary>
    /// <returns>A display snapshot.</returns>
    MatchSnapshot Snapshot();

    /// <summary>Recent event lines, oldest first.</summary>
    /// <returns>A snapshot copy.</returns>
    IReadOnlyList<string> RecentEvents();

    /// <summary>Starts a match with the online roster as the lobby.</summary>
    /// <param name="mode">The configured game mode.</param>
    /// <returns>An error message, or <see langword="null"/> on success.</returns>
    string? StartMatch(IGameMode mode);

    /// <summary>Stops the current match.</summary>
    void Stop();

    /// <summary>Sends an ad-hoc control message.</summary>
    /// <param name="control">The control to send.</param>
    void SendControl(Control control);

    /// <summary>Assigns a device's team (0 = neutral, 1..4 = a side).</summary>
    /// <param name="deviceId">The device id from the roster.</param>
    /// <param name="team">The team to assign.</param>
    /// <returns>An error message, or <see langword="null"/> on success.</returns>
    /// <remarks>
    /// Teams live in each device's persisted config, not in the control plane,
    /// so this is a REST call to the board and takes effect from the next
    /// match (the lobby fixes teams at start).
    /// </remarks>
    Task<string?> SetTeamAsync(string deviceId, int team);

    /// <summary>
    /// True once any telemetry datagram has arrived.
    /// </summary>
    /// <remarks>
    /// An empty roster is ambiguous: no boards powered, a missing firewall
    /// rule, or (on Android) a missing multicast lock. This lets the UI say
    /// which rather than showing an unexplained empty table.
    /// </remarks>
    bool HasReceivedTelemetry { get; }

    /// <summary>Where CTL messages are being broadcast, for display.</summary>
    string BroadcastTarget { get; }

    /// <summary>Raised after each engine tick so the UI can re-render.</summary>
    event Action? Changed;
}
