using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// The engine-provided view a game mode operates on: current time, match
/// state, and outbound-control/scoring callbacks. Constructed by the engine
/// per delegated call; modes must not retain it across calls.
/// </summary>
public sealed class MatchContext
{
    private readonly Action<int, int> _addScore;
    private readonly Action<Control> _send;

    /// <summary>Initializes the context (engine-internal; public for tests).</summary>
    /// <param name="now">The current time per the engine's clock.</param>
    /// <param name="matchStartedAt">When the Running phase began.</param>
    /// <param name="startHp">The hp participants (re)spawn with.</param>
    /// <param name="participants">The current participants.</param>
    /// <param name="scores">The current per-team scores.</param>
    /// <param name="addScore">Callback adding points to a team.</param>
    /// <param name="send">Callback queueing an outbound control message.</param>
    public MatchContext(
        DateTimeOffset now,
        DateTimeOffset matchStartedAt,
        int startHp,
        IReadOnlyList<Participant> participants,
        IReadOnlyDictionary<int, int> scores,
        Action<int, int> addScore,
        Action<Control> send)
    {
        Now = now;
        MatchStartedAt = matchStartedAt;
        StartHp = startHp;
        Participants = participants;
        Scores = scores;
        _addScore = addScore;
        _send = send;
    }

    /// <summary>Gets the current time (injected clock — never read the system clock in a mode).</summary>
    public DateTimeOffset Now { get; }

    /// <summary>Gets when the Running phase began.</summary>
    public DateTimeOffset MatchStartedAt { get; }

    /// <summary>Gets the hp value participants (re)spawn with.</summary>
    public int StartHp { get; }

    /// <summary>Gets the current participants.</summary>
    public IReadOnlyList<Participant> Participants { get; }

    /// <summary>Gets the current per-team scores.</summary>
    public IReadOnlyDictionary<int, int> Scores { get; }

    /// <summary>Adds points to a team's score.</summary>
    /// <param name="team">The team index.</param>
    /// <param name="points">Points to add.</param>
    public void AddScore(int team, int points) => _addScore(team, points);

    /// <summary>Queues an outbound control message (sent via the engine's <c>IControlSender</c>).</summary>
    /// <param name="control">The control to send.</param>
    public void Send(Control control) => _send(control);
}
