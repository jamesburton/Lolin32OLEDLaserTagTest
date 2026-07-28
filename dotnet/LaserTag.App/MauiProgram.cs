using System.Net;
using LaserTag.Client;
using LaserTag.Runtime;
using LaserTag.Ui;
using Microsoft.Extensions.Logging;

namespace LaserTag.App;

/// <summary>Builds the MAUI application host.</summary>
public static class MauiProgram
{
    /// <summary>Creates and configures the app.</summary>
    /// <returns>The configured application.</returns>
    public static MauiApp CreateMauiApp()
    {
        MauiAppBuilder builder = MauiApp.CreateBuilder();
        builder
            .UseMauiApp<App>()
            .ConfigureFonts(fonts =>
            {
                fonts.AddFont("OpenSans-Regular.ttf", "OpenSansRegular");
            });

        builder.Services.AddMauiBlazorWebView();

        // The phone IS the host: the engine, the UDP listener and the CTL
        // broadcaster all run in this process, so no PC is needed at play time.
        IPEndPoint broadcast = BroadcastAddress.DiscoverLocalBroadcastEndpoint(UdpTelemetryService.Port)
            ?? new IPEndPoint(IPAddress.Broadcast, UdpTelemetryService.Port);

#if ANDROID
        IPlatformNetworkGuard guard = new Platforms.Android.AndroidMulticastGuard();
#else
        IPlatformNetworkGuard guard = IPlatformNetworkGuard.Null;
#endif

        builder.Services.AddLaserTagRuntime(broadcast, guard);
        builder.Services.AddSingleton<IGameSession>(sp => new LocalGameSession(
            sp.GetRequiredService<GameService>(),
            sp.GetRequiredService<UdpTelemetryService>(),
            broadcast.ToString()));

#if DEBUG
        builder.Services.AddBlazorWebViewDeveloperTools();
        builder.Logging.AddDebug();
#endif

        return builder.Build();
    }
}
