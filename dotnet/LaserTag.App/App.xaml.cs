using Microsoft.Extensions.Hosting;

namespace LaserTag.App;

/// <summary>The MAUI application shell.</summary>
public partial class App : Application
{
    private readonly IEnumerable<IHostedService> _hostedServices;
    private readonly CancellationTokenSource _shutdown = new();

    /// <summary>Initializes the app and its background services.</summary>
    /// <param name="hostedServices">
    /// The runtime's background services (UDP listener and engine tick).
    /// </param>
    public App(IEnumerable<IHostedService> hostedServices)
    {
        InitializeComponent();
        _hostedServices = hostedServices;
    }

    /// <inheritdoc/>
    protected override Window CreateWindow(IActivationState? activationState) =>
        new(new MainPage()) { Title = "Laser Tag" };

    /// <inheritdoc/>
    protected override void OnStart()
    {
        base.OnStart();

        // MAUI builds a service provider but, unlike the Generic Host, never
        // starts IHostedService instances. Without this the UDP listener and
        // the 4 Hz engine tick simply never run and the app shows a permanently
        // empty roster — so the shared runtime is started by hand here.
        foreach (IHostedService service in _hostedServices)
        {
            _ = service.StartAsync(_shutdown.Token);
        }
    }

    /// <inheritdoc/>
    protected override void OnSleep()
    {
        base.OnSleep();

        // Deliberately left running: matches last minutes and a backgrounded
        // listener that stopped would silently miss hits. Chase mode's
        // device-side self-timeout already tolerates host gaps, but losing
        // telemetry entirely would corrupt the score.
    }
}
