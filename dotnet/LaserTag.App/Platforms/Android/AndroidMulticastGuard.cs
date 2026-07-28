using Android.Content;
using Android.Net.Wifi;
using LaserTag.Runtime;

namespace LaserTag.App.Platforms.Android;

/// <summary>
/// Holds a <see cref="WifiManager.MulticastLock"/> while the UDP listener runs.
/// </summary>
/// <remarks>
/// <para>
/// Android's Wi-Fi stack filters out packets not addressed to the device's own
/// MAC before they reach the socket, which silently includes the subnet
/// broadcasts the boards send. The app therefore receives <i>nothing</i> —
/// no error, no exception, just an empty roster — unless it holds a multicast
/// lock. This is the single most likely cause of "it works on the PC but the
/// phone sees no boards".
/// </para>
/// <para>
/// The lock costs battery (it disables a Wi-Fi power optimisation), so it is
/// acquired only while the listener is running rather than for the app's
/// lifetime.
/// </para>
/// </remarks>
public sealed class AndroidMulticastGuard : IPlatformNetworkGuard
{
    private WifiManager.MulticastLock? _lock;

    /// <inheritdoc/>
    public void Acquire()
    {
        if (_lock is not null)
        {
            return;
        }

        var wifi = (WifiManager?)global::Android.App.Application.Context
            .GetSystemService(Context.WifiService);
        _lock = wifi?.CreateMulticastLock("lasertag-telemetry");

        // Reference-counting off: Acquire/Release are called once each around
        // the listener's lifetime, so a strict pairing is simpler to reason
        // about than a counted lock.
        if (_lock is not null)
        {
            _lock.SetReferenceCounted(false);
            _lock.Acquire();
        }
    }

    /// <inheritdoc/>
    public void Release()
    {
        if (_lock is { IsHeld: true })
        {
            _lock.Release();
        }

        _lock?.Dispose();
        _lock = null;
    }
}
