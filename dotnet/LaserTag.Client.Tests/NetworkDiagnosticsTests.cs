using LaserTag.Client;

namespace LaserTag.Client.Tests;

/// <summary>Tests for the advisory <see cref="NetworkDiagnostics"/> helper.</summary>
public class NetworkDiagnosticsTests
{
    [Fact]
    public void TelemetryPort_MatchesContract()
    {
        Assert.Equal(4210, NetworkDiagnostics.TelemetryPort);
    }

    [Fact]
    public void FirewallFixCommand_Windows_PointsAtPs1()
    {
        string cmd = NetworkDiagnostics.FirewallFixCommand(NetworkDiagnostics.OSKind.Windows);
        Assert.Contains("setup-firewall.ps1", cmd);
        Assert.Contains("powershell", cmd);
    }

    [Theory]
    [InlineData(NetworkDiagnostics.OSKind.Linux)]
    [InlineData(NetworkDiagnostics.OSKind.MacOS)]
    [InlineData(NetworkDiagnostics.OSKind.Unknown)]
    public void FirewallFixCommand_NonWindows_PointsAtShellScript(NetworkDiagnostics.OSKind os)
    {
        string cmd = NetworkDiagnostics.FirewallFixCommand(os);
        Assert.Contains("setup-firewall.sh", cmd);
        Assert.Contains("bash", cmd);
    }

    [Fact]
    public void NoHeartbeatHint_IncludesPortAndFixCommand()
    {
        string hint = NetworkDiagnostics.NoHeartbeatHint(NetworkDiagnostics.OSKind.Windows);
        Assert.Contains("4210", hint);
        Assert.Contains("setup-firewall.ps1", hint);
        Assert.Contains("firewall", hint, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void CurrentOS_ReturnsAKnownValueForThisHost()
    {
        // This test host is Windows/Linux/macOS — never Unknown in CI.
        Assert.NotEqual(NetworkDiagnostics.OSKind.Unknown, NetworkDiagnostics.CurrentOS());
    }
}
