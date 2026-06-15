using System.Runtime.InteropServices;

namespace LaserTag.Client;

/// <summary>
/// Advisory helpers for the most common host-side connectivity problem: REST to
/// a device works (PC-initiated, allowed outbound) but no UDP telemetry arrives
/// because an inbound firewall rule for the telemetry port is missing. Pure
/// string/OS logic — it performs no I/O and changes no system state; the actual
/// fix is delegated to the <c>tools/setup-firewall.*</c> scripts.
/// </summary>
public static class NetworkDiagnostics
{
    /// <summary>The UDP port devices broadcast heartbeats/telemetry to (contract §4).</summary>
    public const int TelemetryPort = 4210;

    /// <summary>A coarse operating-system classification for fix-command selection.</summary>
    public enum OSKind
    {
        /// <summary>Windows.</summary>
        Windows,

        /// <summary>Linux.</summary>
        Linux,

        /// <summary>macOS / OSX.</summary>
        MacOS,

        /// <summary>An OS none of the above matched.</summary>
        Unknown,
    }

    /// <summary>Classifies the current runtime operating system.</summary>
    /// <returns>The matching <see cref="OSKind"/>.</returns>
    public static OSKind CurrentOS()
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            return OSKind.Windows;
        }

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
        {
            return OSKind.Linux;
        }

        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
        {
            return OSKind.MacOS;
        }

        return OSKind.Unknown;
    }

    /// <summary>
    /// Returns the command that runs the firewall setup script for the current OS.
    /// </summary>
    /// <returns>A copy-pasteable command string.</returns>
    public static string FirewallFixCommand() => FirewallFixCommand(CurrentOS());

    /// <summary>
    /// Returns the command that runs the firewall setup script for the given OS.
    /// </summary>
    /// <param name="os">The target operating system.</param>
    /// <returns>A copy-pasteable command string.</returns>
    public static string FirewallFixCommand(OSKind os) => os switch
    {
        OSKind.Windows => @"powershell -ExecutionPolicy Bypass -File tools\setup-firewall.ps1",
        _ => "bash tools/setup-firewall.sh",
    };

    /// <summary>
    /// Builds the hint to show when REST reaches a device but no UDP heartbeats
    /// arrive. Two causes are common: an inbound firewall block on this host, or
    /// packet loss on a weak Wi-Fi link (telemetry is broadcast UDP and lossy at
    /// low RSSI). The hint names both and how to rule out each.
    /// </summary>
    /// <returns>A multi-line, user-facing hint for the current OS.</returns>
    public static string NoHeartbeatHint() => NoHeartbeatHint(CurrentOS());

    /// <summary>
    /// Builds the no-heartbeat hint for a specific OS.
    /// </summary>
    /// <param name="os">The target operating system.</param>
    /// <returns>A multi-line, user-facing hint.</returns>
    public static string NoHeartbeatHint(OSKind os)
    {
        string nl = Environment.NewLine;
        return
            $"REST reached the device but no UDP heartbeats arrived on port {TelemetryPort}." + nl +
            "Two causes are common - rule each out:" + nl +
            $"  - inbound firewall block on this host:  {FirewallFixCommand(os)}" + nl +
            "  - lossy Wi-Fi link (broadcast UDP drops at low RSSI): move closer to the AP, " +
            "or check the device's rssi via GET /api/status.";
    }
}
