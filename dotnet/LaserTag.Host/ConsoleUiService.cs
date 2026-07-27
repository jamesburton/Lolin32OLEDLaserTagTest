using LaserTag.Client;
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
        AnsiConsole.MarkupLine("[bold]LaserTag host[/] — commands: devices, start dm <dur> [[--kill N]] [[--hit N]] [[--waves <dur>]], start elim [[--timer <dur>]], start chase <dur|--first N> [[--min d]] [[--max d]] [[--gap d]] [[--penalty N]] [[--dark]], score, stop, reset [[id]], activate [[id]], deactivate [[id]], fw [[bin]], ota <id|all> [[--force]] [[bin]], quit");
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
            case "fw":
                PrintFirmware(args.ElementAtOrDefault(1));
                break;
            case "ota":
                RunOta(args);
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
        else if (kind == "chase")
        {
            TimeSpan? duration = DurationParser.TryParse(args.ElementAtOrDefault(2), out TimeSpan d) ? d : null;
            int? firstTo = IntOption(args, "--first");
            if (duration is null && firstTo is null)
            {
                AnsiConsole.MarkupLine("[yellow]usage: start chase <dur and/or --first N> [[--min d]] [[--max d]] [[--gap d]] [[--penalty N]] [[--dark]][/]");
                return;
            }

            mode = new ChaseMode(
                duration,
                firstTo,
                DurationOption(args, "--min"),
                DurationOption(args, "--max"),
                DurationOption(args, "--gap"),
                IntOption(args, "--penalty") ?? 0,
                args.Contains("--dark") ? "dark" : "score");
        }
        else
        {
            AnsiConsole.MarkupLine("[yellow]usage: start dm <dur> | start elim | start chase[/]");
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

    // Default firmware image: the OTA build's bin, falling back to the plain
    // env (fleet-ota spec).
    private static readonly string[] DefaultBins =
    [
        Path.Combine(".pio", "build", "esp32-s3-matrix-ota", "firmware.bin"),
        Path.Combine(".pio", "build", "esp32-s3-matrix", "firmware.bin"),
    ];

    private static string? ResolveBin(string? arg)
    {
        if (arg is { } p && !p.StartsWith("--", StringComparison.Ordinal))
        {
            return File.Exists(p) ? p : null;
        }

        return DefaultBins.FirstOrDefault(File.Exists);
    }

    private void PrintFirmware(string? pathArg)
    {
        string? bin = ResolveBin(pathArg);
        string? available = bin is null ? null : FirmwareImage.TryReadVersion(bin);
        AnsiConsole.MarkupLineInterpolated(
            $"available: [bold]{available ?? "unknown"}[/] {(bin is null ? "(no firmware.bin found — build first)" : $"({bin})")}");
        var table = new Table().AddColumns("id", "host", "ip", "running", "verdict");
        foreach (var e in game.Devices())
        {
            Heartbeat hb = e.LastHeartbeat;
            FirmwareVerdict verdict = FirmwareImage.Compare(hb.Fw, available);
            string colour = verdict switch
            {
                FirmwareVerdict.Current => "green",
                FirmwareVerdict.Outdated => "red",
                FirmwareVerdict.Newer => "yellow",
                _ => "grey",
            };
            table.AddRow(e.Id, hb.Source, hb.Ip, hb.Fw, $"[{colour}]{verdict}[/]");
        }

        AnsiConsole.Write(table);
    }

    private void RunOta(string[] args)
    {
        string? target = args.ElementAtOrDefault(1);
        if (target is null)
        {
            AnsiConsole.MarkupLine("[yellow]usage: ota <id|all> [[--force]] [[path-to-firmware.bin]][/]");
            return;
        }

        bool force = args.Contains("--force");
        string? bin = ResolveBin(args.Skip(2).FirstOrDefault(a => !a.StartsWith("--", StringComparison.Ordinal)));
        if (bin is null)
        {
            AnsiConsole.MarkupLine("[red]no firmware.bin found — build (pio run -e esp32-s3-matrix-ota) or pass a path.[/]");
            return;
        }

        string? available = FirmwareImage.TryReadVersion(bin);
        var candidates = game.Devices()
            .Where(e => e.Online)
            .Where(e => target == "all" ? force || FirmwareImage.Compare(e.LastHeartbeat.Fw, available) == FirmwareVerdict.Outdated
                                        : e.Id == target)
            .ToList();
        if (candidates.Count == 0)
        {
            AnsiConsole.MarkupLine(target == "all"
                ? "[green]fleet is current — nothing to update (use --force to re-push).[/]"
                : $"[red]no online device with id {target}.[/]");
            return;
        }

        AnsiConsole.MarkupLineInterpolated($"pushing [bold]{available ?? "?"}[/] ({bin}) to {candidates.Count} device(s)…");
        var updater = new FirmwareUpdater();

        // Sequential on purpose: parallel flash writes on a shared 2.4 GHz
        // channel mostly just fight each other for airtime.
        foreach (var e in candidates)
        {
            AnsiConsole.MarkupLineInterpolated($"  {e.Id} ({e.LastHeartbeat.Ip}) … ");
            FirmwareUpdater.Result r = updater
                .UploadAsync(e.LastHeartbeat.Ip, bin, CancellationToken.None)
                .GetAwaiter()
                .GetResult();
            AnsiConsole.MarkupLineInterpolated(r.Ok
                ? $"  [green]{e.Id} OK[/] — rebooting into the new image"
                : $"  [red]{e.Id} FAILED[/]: {r.Error} (pre-2.1.0 firmware has no /api/update — flash once via espota)");
        }
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
