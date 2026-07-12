using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// A pluggable game mode: the engine owns lifecycle/state mirroring, the mode
/// owns rules — scoring, respawn policy, and the win condition.
/// </summary>
public interface IGameMode
{
    /// <summary>Gets the display name (e.g. <c>deathmatch</c>).</summary>
    string Name { get; }

    /// <summary>Gets the fixed match length, or <see langword="null"/> for untimed modes.</summary>
    TimeSpan? MatchDuration { get; }

    /// <summary>Called once when the Running phase begins.</summary>
    /// <param name="context">The match context.</param>
    void OnMatchStart(MatchContext context);

    /// <summary>
    /// Called for each hit during Running, after the engine has updated the
    /// victim's hp/alive state (so <c>hit.Hp == 0</c> means this shot killed).
    /// </summary>
    /// <param name="context">The match context.</param>
    /// <param name="hit">The hit event.</param>
    void OnHit(MatchContext context, HitEvent hit);

    /// <summary>Called for each device state event during Running.</summary>
    /// <param name="context">The match context.</param>
    /// <param name="state">The state event.</param>
    /// <param name="participant">The participant it came from.</param>
    void OnDeviceState(MatchContext context, StateEvent state, Participant participant);

    /// <summary>Called on every engine tick during Running (respawn scheduling etc.).</summary>
    /// <param name="context">The match context.</param>
    void OnTick(MatchContext context);

    /// <summary>
    /// Checks the win condition. Called after every event and tick.
    /// </summary>
    /// <param name="context">The match context.</param>
    /// <returns>The result when the match should end, else <see langword="null"/>.</returns>
    MatchResult? CheckEnd(MatchContext context);
}
