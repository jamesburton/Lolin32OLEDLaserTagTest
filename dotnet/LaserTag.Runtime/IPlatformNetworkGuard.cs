namespace LaserTag.Runtime;

/// <summary>
/// Platform hook wrapping the UDP listener's lifetime.
/// </summary>
/// <remarks>
/// Exists for one reason: <b>Android silently discards inbound broadcast UDP
/// unless the app holds a <c>WifiManager.MulticastLock</c></b>. Without it the
/// app looks perfectly healthy and simply never sees a heartbeat — the classic
/// "works on the PC, dead on the phone" failure. Making it an explicit
/// dependency of the listener means the platform that needs it cannot forget
/// it, and desktop hosts opt out through <see cref="Null"/> rather than by
/// silence.
/// </remarks>
public interface IPlatformNetworkGuard
{
    /// <summary>Acquires any platform resources the UDP listener needs.</summary>
    void Acquire();

    /// <summary>Releases them again.</summary>
    void Release();

    /// <summary>A guard that does nothing, for platforms with no such requirement.</summary>
    public static IPlatformNetworkGuard Null { get; } = new NullGuard();

    /// <summary>The desktop no-op implementation.</summary>
    private sealed class NullGuard : IPlatformNetworkGuard
    {
        /// <inheritdoc/>
        public void Acquire()
        {
        }

        /// <inheritdoc/>
        public void Release()
        {
        }
    }
}
