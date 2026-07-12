using LaserTag.Client.Models;
using LaserTag.Game;
using Microsoft.Extensions.Hosting;
using Spectre.Console;

namespace LaserTag.Host;

/// <summary>
/// The interactive REPL: reads commands from stdin, prints the event feed and
/// scoreboard via Spectre.Console.
/// </summary>
public sealed class ConsoleUiService(GameService game, IHostApplicationLifetime lifetime) : BackgroundService
{
    /// <inheritdoc/>
    protected override Task ExecuteAsync(CancellationToken stoppingToken)
    {
        game.Event += line => AnsiConsole.MarkupLineInterpolated($"[grey]{DateTime.Now:HH:mm:ss}[/] {line}");
        return Task.Run(() => Repl(stoppingToken), stoppingToken);
    }

    private void Repl(CancellationToken stoppingToken)
    {
        AnsiConsole.MarkupLine("[bold]LaserTag host[/] — commands: devices, start dm <dur> [[--kill N]] [[--hit N]] [[--waves <dur>]], start elim [[--timer <dur>]], score, stop, reset [[id]], activate [[id]], deactivate [[id]], quit");
        while (!stoppingToken.IsCancellationRequested)
        {
            string? line = Console.ReadLine();
            if (line is null)
            {
                break;
            }

            try
            {
                if (!Dispatch(line.Trim()))
                {
                    break; // quit
                }
            }
            catch (Exception ex)
            {
                AnsiConsole.MarkupLineInterpolated($"[red]error:[/] {ex.Message}");
            }
        }

        lifetime.StopApplication();
    }

    private bool Dispatch(string line)
    {
        string[] args = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (args.Length == 0)
        {
            return true;
        }

        switch (args[0].ToLowerInvariant())
        {
            case "quit" or "exit":
                return false;
            case "devices":
                PrintDevices();
                break;
            case "score":
                PrintScore();
                break;
            case "stop":
                game.Stop();
                break;
            case "start":
                StartMatch(args);
                break;
            case "reset":
                game.SendControl(new Control { Kind = ControlKind.Reset, Id = args.ElementAtOrDefault(1) });
                break;
            case "activate":
                game.SendControl(new Control { Kind = ControlKind.Activate, Id = args.ElementAtOrDefault(1) });
                break;
            case "deactivate":
                game.SendControl(new Control { Kind = ControlKind.Deactivate, Id = args.ElementAtOrDefault(1) });
                break;
            default:
                AnsiConsole.MarkupLine("[yellow]unknown command[/]");
                break;
        }

        return true;
    }

    private void StartMatch(string[] args)
    {
        string? kind = args.ElementAtOrDefault(1)?.ToLowerInvariant();
        IGameMode mode;
        if (kind == "dm")
        {
            if (!DurationParser.TryParse(args.ElementAtOrDefault(2), out TimeSpan duration))
            {
                AnsiConsole.MarkupLine("[yellow]usage: start dm <duration e.g. 5m>[/]");
                return;
            }

            int hit = IntOption(args, "--hit") ?? 1;
            int kill = IntOption(args, "--kill") ?? 5;
            TimeSpan? waves = DurationOption(args, "--waves");
            mode = new DeathmatchMode(duration, hit, kill, waveInterval: waves);
        }
        else if (kind == "elim")
        {
            mode = new EliminationMode(DurationOption(args, "--timer"));
        }
        else
        {
            AnsiConsole.MarkupLine("[yellow]usage: start dm <dur> | start elim[/]");
            return;
        }

        string? error = game.StartMatch(mode);
        AnsiConsole.MarkupLine(error is null
            ? $"[green]{mode.Name} starting — countdown![/]"
            : $"[red]{error}[/]");
    }

    private static int? IntOption(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length && int.TryParse(args[i + 1], out int v) ? v : null;
    }

    private static TimeSpan? DurationOption(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length && DurationParser.TryParse(args[i + 1], out TimeSpan v) ? v : null;
    }

    private void PrintDevices()
    {
        var table = new Table().AddColumns("id", "host", "ip", "team", "hp", "online");
        foreach (var e in game.Devices())
        {
            Heartbeat hb = e.LastHeartbeat;
            table.AddRow(e.Id, hb.Source, hb.Ip, hb.Team.ToString(), hb.Hp.ToString(), e.Online ? "yes" : "[red]no[/]");
        }

        AnsiConsole.Write(table);
    }

    private void PrintScore()
    {
        MatchSnapshot s = game.Snapshot();
        AnsiConsole.MarkupLineInterpolated(
            $"[bold]{s.ModeName}[/] phase={s.Phase} elapsed={s.Elapsed:mm\\:ss}{(s.Remaining is { } r ? $" remaining={r:mm\\:ss}" : string.Empty)}{(s.Winner is { } w ? $" winner={(w == 0 ? "draw" : $"team {w}")}" : string.Empty)}");
        var scores = new Table().AddColumns("team", "score");
        foreach ((int team, int pts) in s.TeamScores.OrderByDescending(kv => kv.Value))
        {
            scores.AddRow($"team {team}", pts.ToString());
        }

        AnsiConsole.Write(scores);
        string aliveLine = string.Join(", ", s.Participants
            .GroupBy(p => p.Team)
            .OrderBy(g => g.Key)
            .Select(g => $"team {g.Key}: {g.Count(p => p.Alive && p.Online)}"));
        AnsiConsole.MarkupLineInterpolated($"alive: {aliveLine}");
        var players = new Table().AddColumns("id", "host", "team", "hp", "alive", "online");
        foreach (Participant p in s.Participants.OrderBy(p => p.Team))
        {
            players.AddRow(p.Id, p.Hostname, p.Team.ToString(), p.Hp.ToString(), p.Alive ? "yes" : "[red]dead[/]", p.Online ? "yes" : "[red]no[/]");
        }

        AnsiConsole.Write(players);
    }
}
