using Microsoft.AspNetCore.Components;

namespace LaserTag.Ui.Shared;

/// <summary>
/// Base for screens that must re-render whenever the match state moves.
/// </summary>
/// <remarks>
/// Subscribes to <see cref="IGameSession.Changed"/>, which fires on the engine's
/// 4 Hz tick from a background thread — hence the <c>InvokeAsync</c> hop onto
/// the renderer's synchronisation context. Unsubscribing on dispose matters
/// more than usual here: the session outlives every page, so a leaked handler
/// would keep re-rendering a dead component for the rest of the process.
/// </remarks>
public abstract class LiveComponentBase : ComponentBase, IDisposable
{
    /// <summary>The session driving this screen.</summary>
    [Inject]
    public IGameSession Session { get; set; } = default!;

    /// <inheritdoc/>
    protected override void OnInitialized() => Session.Changed += OnChanged;

    /// <summary>Requests a re-render on the UI thread.</summary>
    private void OnChanged() => _ = InvokeAsync(StateHasChanged);

    /// <inheritdoc/>
    public void Dispose()
    {
        Session.Changed -= OnChanged;
        GC.SuppressFinalize(this);
    }
}
