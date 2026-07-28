using LaserTag.Client;
using LaserTag.Client.Models;
using LaserTag.Game;
using LaserTag.Runtime;

namespace LaserTag.Ui;

/// <summary>
/// An <see cref="IGameSession"/> backed by an in-process <see cref="GameService"/>.
/// </summary>
/// <remarks>
/// Used by both shipping shells: the web manager (engine on the PC) and the
/// Android app (engine on the phone). The only difference between them is which
/// machine this object lives on.
/// </remarks>
/// <param name="game">The shared game service.</param>
/// <param name="telemetry">The UDP listener, consulted for liveness.</param>
/// <param name="broadcastTarget">The CTL broadcast endpoint, for display.</param>
public sealed class LocalGameSession(GameService game, UdpTelemetryService telemetry, string broadcastTarget)
    : IGameSession, IDisposable
{
    /// <inheritdoc/>
    public event Action? Changed
    {
        add => game.StateChanged += value;
        remove => game.StateChanged -= value;
    }

    /// <inheritdoc/>
    public bool HasReceivedTelemetry => telemetry.HasReceivedAny;

    /// <inheritdoc/>
    public string BroadcastTarget => broadcastTarget;

    /// <inheritdoc/>
    public IReadOnlyList<RosterEntry> Devices() => game.Devices();

    /// <inheritdoc/>
    public MatchSnapshot Snapshot() => game.Snapshot();

    /// <inheritdoc/>
    public IReadOnlyList<string> RecentEvents() => game.RecentEvents();

    /// <inheritdoc/>
    public string? StartMatch(IGameMode mode) => game.StartMatch(mode);

    /// <inheritdoc/>
    public void Stop() => game.Stop();

    /// <inheritdoc/>
    public void SendControl(Control control) => game.SendControl(control);

    /// <inheritdoc/>
    public void Dispose()
    {
    }
}
