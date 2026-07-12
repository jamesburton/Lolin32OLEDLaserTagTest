using LaserTag.Client.Models;

namespace LaserTag.Client;

/// <summary>
/// Sends host→device <see cref="Control"/> messages. Implementations own the
/// transport (UDP subnet broadcast in production, in-memory fakes in tests).
/// </summary>
public interface IControlSender
{
    /// <summary>
    /// Sends a control message, repeating it per the implementation's
    /// reliability policy (CTL is lossy fire-and-forget UDP).
    /// </summary>
    /// <param name="control">The control message to send.</param>
    /// <param name="cancellationToken">Cancels the send (including repeats).</param>
    /// <returns>A task that completes when all repeats have been handed to the transport.</returns>
    Task SendAsync(Control control, CancellationToken cancellationToken = default);
}
