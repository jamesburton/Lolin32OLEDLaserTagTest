using Microsoft.Extensions.Hosting;

namespace LaserTag.Host;

/// <summary>Drives <see cref="GameService.Tick"/> every 250 ms.</summary>
public sealed class MatchEngineService(GameService game) : BackgroundService
{
    /// <inheritdoc/>
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(250));
        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken).ConfigureAwait(false))
            {
                game.Tick();
            }
        }
        catch (OperationCanceledException)
        {
        }
    }
}
