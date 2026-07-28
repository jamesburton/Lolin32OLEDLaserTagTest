using System.Net;
using LaserTag.Client;
using Microsoft.Extensions.DependencyInjection;

namespace LaserTag.Runtime;

/// <summary>
/// Registers the shared match runtime in a DI container.
/// </summary>
/// <remarks>
/// Every shell — console REPL, web manager, Android app — wires up the same
/// three pieces. Keeping that in one place is what stops the shells drifting
/// apart in behaviour while looking alike.
/// </remarks>
public static class ServiceCollectionExtensions
{
    /// <summary>Adds the game service, UDP listener and tick loop.</summary>
    /// <param name="services">The service collection.</param>
    /// <param name="broadcast">Where CTL messages are broadcast.</param>
    /// <param name="guard">
    /// Platform guard for the UDP listener; defaults to the desktop no-op.
    /// Android must pass a multicast-lock implementation or it will receive
    /// nothing.
    /// </param>
    /// <returns>The same collection, for chaining.</returns>
    public static IServiceCollection AddLaserTagRuntime(
        this IServiceCollection services,
        IPEndPoint broadcast,
        IPlatformNetworkGuard? guard = null)
    {
        services.AddSingleton<IControlSender>(new UdpControlSender(broadcast));
        services.AddSingleton(guard ?? IPlatformNetworkGuard.Null);
        services.AddSingleton<GameService>();

        // Registered as singletons *and* as hosted services so a shell can also
        // resolve them directly — the web UI asks the listener whether it has
        // ever received a datagram, to explain an empty roster honestly.
        services.AddSingleton<UdpTelemetryService>();
        services.AddSingleton<MatchEngineService>();
        services.AddHostedService(sp => sp.GetRequiredService<UdpTelemetryService>());
        services.AddHostedService(sp => sp.GetRequiredService<MatchEngineService>());
        return services;
    }
}
