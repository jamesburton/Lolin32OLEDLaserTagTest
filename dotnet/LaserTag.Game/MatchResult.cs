namespace LaserTag.Game;

/// <summary>A finished match's outcome.</summary>
/// <param name="WinnerTeam">The winning team, or <c>0</c> for a draw.</param>
public sealed record MatchResult(int WinnerTeam);
