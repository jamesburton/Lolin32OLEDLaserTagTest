using System.Net;
using LaserTag.Client;
using LaserTag.Host;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Spectre.Console;

// --broadcast <ip> overrides NIC discovery (e.g. multiple adapters).
IPEndPoint? broadcast = null;
int flag = Array.IndexOf(args, "--broadcast");
if (flag >= 0 && flag + 1 < args.Length)
{
    if (!IPAddress.TryParse(args[flag + 1], out IPAddress? parsed))
    {
        AnsiConsole.MarkupLineInterpolated($"[red]Invalid --broadcast address: {args[flag + 1]}[/]");
        return 1;
    }

    broadcast = new IPEndPoint(parsed, UdpTelemetryService.Port);
}

broadcast ??= BroadcastAddress.DiscoverLocalBroadcastEndpoint(UdpTelemetryService.Port);
if (broadcast is null)
{
    AnsiConsole.MarkupLine("[red]No usable IPv4 NIC found — pass --broadcast <subnet-broadcast-ip>.[/]");
    return 1;
}

AnsiConsole.MarkupLineInterpolated($"CTL broadcast target: [bold]{broadcast}[/]");

HostApplicationBuilder builder = Host.CreateApplicationBuilder(args);
builder.Logging.ClearProviders(); // keep the console clean for the REPL
builder.Services.AddSingleton<IControlSender>(new UdpControlSender(broadcast));
builder.Services.AddSingleton<GameService>();
builder.Services.AddHostedService<UdpTelemetryService>();
builder.Services.AddHostedService<MatchEngineService>();
builder.Services.AddHostedService<ConsoleUiService>();

await builder.Build().RunAsync();
return 0;
