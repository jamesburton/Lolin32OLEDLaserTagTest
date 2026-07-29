using LaserTag.Client;
using LaserTag.Client.Models;
using LaserTag.Game;
using LaserTag.Runtime;
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
        AnsiConsole.MarkupLine("[bold]LaserTag host[/] — commands: devices, start dm <dur> [[--kill N]] [[--hit N]] [[--waves <dur>]], start elim [[--timer <dur>]], start chase <dur|--first N> [[--min d]] [[--max d]] [[--gap d]] [[--penalty N]] [[--dark]], score, stop, reset [[id]], activate [[id]], deactivate [[id]], team <id|all> <0-4|none>, teams split <n>, sd <ls|put|get|rm|play|startup> …, fw [[bin]], ota <id|all> [[--force]] [[bin]], quit");
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
            case "team" or "teams":
                RunTeam(args);
                break;
            case "sd":
                RunSd(args);
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
            if (r.Ok)
            {
                AnsiConsole.MarkupLineInterpolated($"  [green]{e.Id} OK[/] — rebooting into the new image");
            }
            else
            {
                AnsiConsole.MarkupLineInterpolated($"  [red]{e.Id} FAILED[/]: {r.Error} (pre-2.1.0 firmware has no /api/update — flash once via espota)");
            }
        }
    }

    // team <id|all> <0-4|none>   — assign one board or the whole roster
    // teams split <n>            — deal the online roster round-robin into n sides
    private void RunTeam(string[] args)
    {
        if (args.ElementAtOrDefault(1)?.ToLowerInvariant() == "split")
        {
            RunTeamSplit(args.ElementAtOrDefault(2));
            return;
        }

        string? target = args.ElementAtOrDefault(1);
        if (target is null || !Teams.TryParse(args.ElementAtOrDefault(2), out int team))
        {
            AnsiConsole.MarkupLine("[yellow]usage: team <id|all> <0-4|none>  |  teams split <n>[/]");
            return;
        }

        bool all = target.Equals("all", StringComparison.OrdinalIgnoreCase);
        var targets = game.Devices().Where(e => all || e.Id == target).ToList();
        if (targets.Count == 0)
        {
            // Distinguish "the roster is empty" from "that id isn't here":
            // an empty roster after a fresh start usually just means the
            // heartbeats have not landed yet (they can take ~30-60 s).
            AnsiConsole.MarkupLine(all
                ? "[red]no devices in the roster yet[/] — wait for heartbeats and retry."
                : $"[red]no device with id {target}.[/]");
            return;
        }

        AnsiConsole.MarkupLineInterpolated(
            $"assigning team [bold]{Teams.Describe(team)}[/] to {targets.Count} device(s)…");
        foreach (var e in targets)
        {
            Assign(e.Id, team);
        }
    }

    private void RunTeamSplit(string? countText)
    {
        if (!int.TryParse(countText, out int sides) || sides < 2 || sides > Teams.Max)
        {
            AnsiConsole.MarkupLineInterpolated($"[yellow]usage: teams split <2-{Teams.Max}>[/]");
            return;
        }

        // Deal round-robin over a STABLE order (by id) so the same fleet always
        // splits the same way — a random or roster-order split would silently
        // reshuffle sides between matches.
        var online = game.Devices().Where(e => e.Online).OrderBy(e => e.Id, StringComparer.Ordinal).ToList();
        if (online.Count == 0)
        {
            AnsiConsole.MarkupLine("[red]no online devices to split[/] — wait for heartbeats and retry.");
            return;
        }

        // Say how many are being split. A split that silently covers only the
        // boards discovered so far looks identical to a split of the whole
        // fleet, and leaves the rest on whatever team they had.
        AnsiConsole.MarkupLineInterpolated(
            $"splitting [bold]{online.Count}[/] online device(s) into {sides} team(s)…");
        for (int i = 0; i < online.Count; i++)
        {
            Assign(online[i].Id, (i % sides) + 1);
        }
    }

    private void Assign(string id, int team)
    {
        string? error = game.SetTeamAsync(id, team).GetAwaiter().GetResult();
        if (error is null)
        {
            AnsiConsole.MarkupLineInterpolated($"  [green]{id}[/] -> team {Teams.Describe(team)}");
        }
        else
        {
            AnsiConsole.MarkupLineInterpolated($"  [red]{id} FAILED[/]: {error}");
        }
    }

    private readonly SdCardClient sd = new();

    // sd ls <id> [dir] | sd put <id> <local> <remote> | sd get <id> <remote> <local>
    // sd rm <id> <remote> | sd play <id> <remote> | sd startup <id> <remote|none>
    private void RunSd(string[] args)
    {
        string? verb = args.ElementAtOrDefault(1)?.ToLowerInvariant();
        string? id = args.ElementAtOrDefault(2);
        if (verb is null || id is null)
        {
            AnsiConsole.MarkupLine(
                "[yellow]usage: sd ls <id> [[dir]] | sd put <id> <local> <remote> | " +
                "sd get <id> <remote> <local> | sd rm <id> <remote> | " +
                "sd play <id> <remote> | sd startup <id> <remote|none>[/]");
            return;
        }

        var entry = game.Devices().FirstOrDefault(e => e.Id == id);
        if (entry is null)
        {
            AnsiConsole.MarkupLineInterpolated($"[red]no device with id {id}.[/]");
            return;
        }

        string ip = entry.LastHeartbeat.Ip;
        switch (verb)
        {
            case "ls":
                SdList(ip, args.ElementAtOrDefault(3) ?? "/");
                break;
            case "put":
                SdPut(ip, args.ElementAtOrDefault(3), args.ElementAtOrDefault(4));
                break;
            case "get":
                SdGet(ip, args.ElementAtOrDefault(3), args.ElementAtOrDefault(4));
                break;
            case "rm":
                SdRemove(ip, args.ElementAtOrDefault(3));
                break;
            case "play":
                SdPlay(ip, args.ElementAtOrDefault(3));
                break;
            case "startup":
                SdStartup(ip, args.ElementAtOrDefault(3));
                break;
            default:
                AnsiConsole.MarkupLine("[yellow]unknown sd verb[/]");
                break;
        }
    }

    private void SdList(string ip, string dir)
    {
        SdCardClient.Result<SdListing> r = sd.ListAsync(ip, dir).GetAwaiter().GetResult();
        if (!r.Ok || r.Value is null)
        {
            AnsiConsole.MarkupLineInterpolated($"[red]sd ls failed:[/] {r.Error}");
            return;
        }

        SdListing listing = r.Value;
        AnsiConsole.MarkupLineInterpolated(
            $"card: [bold]{listing.UsedKb / 1024}[/] MB used of [bold]{listing.TotalKb / 1024}[/] MB — {listing.Path}");
        var table = new Table().AddColumns("name", "size", "type");
        foreach (SdEntry e in listing.Files)
        {
            table.AddRow(e.Name, e.IsDirectory ? "-" : e.Size.ToString(), e.IsDirectory ? "dir" : "file");
        }

        AnsiConsole.Write(table);
    }

    private void SdPut(string ip, string? local, string? remote)
    {
        if (local is null || remote is null)
        {
            AnsiConsole.MarkupLine("[yellow]usage: sd put <id> <local> <remote>[/]");
            return;
        }

        SdCardClient.Result<bool> r = sd.UploadAsync(ip, local, remote).GetAwaiter().GetResult();
        // if/else, not a ternary: a ternary collapses both arms to `string`,
        // which binds MarkupLineInterpolated's string overload and re-enables
        // markup parsing of the interpolated values (bit us before, e071754).
        if (r.Ok)
        {
            AnsiConsole.MarkupLineInterpolated($"  [green]uploaded[/] {local} -> {remote}");
        }
        else
        {
            AnsiConsole.MarkupLineInterpolated($"  [red]upload failed:[/] {r.Error}");
        }
    }

    private void SdGet(string ip, string? remote, string? local)
    {
        if (remote is null || local is null)
        {
            AnsiConsole.MarkupLine("[yellow]usage: sd get <id> <remote> <local>[/]");
            return;
        }

        SdCardClient.Result<byte[]> r = sd.DownloadAsync(ip, remote).GetAwaiter().GetResult();
        if (!r.Ok || r.Value is null)
        {
            AnsiConsole.MarkupLineInterpolated($"  [red]download failed:[/] {r.Error}");
            return;
        }

        File.WriteAllBytes(local, r.Value);
        AnsiConsole.MarkupLineInterpolated($"  [green]downloaded[/] {remote} -> {local} ({r.Value.Length} bytes)");
    }

    private void SdRemove(string ip, string? remote)
    {
        if (remote is null)
        {
            AnsiConsole.MarkupLine("[yellow]usage: sd rm <id> <remote>[/]");
            return;
        }

        SdCardClient.Result<bool> r = sd.DeleteAsync(ip, remote).GetAwaiter().GetResult();
        if (r.Ok)
        {
            AnsiConsole.MarkupLineInterpolated($"  [green]deleted[/] {remote}");
        }
        else
        {
            AnsiConsole.MarkupLineInterpolated($"  [red]delete failed:[/] {r.Error}");
        }
    }

    private void SdPlay(string ip, string? remote)
    {
        if (remote is null)
        {
            AnsiConsole.MarkupLine("[yellow]usage: sd play <id> <remote>[/]");
            return;
        }

        using var http = new HttpClient { BaseAddress = new Uri($"http://{ip}"), Timeout = TimeSpan.FromSeconds(15) };
        var client = new LaserTagClient(http);
        try
        {
            bool ok = client.SendCommandAsync(new CommandDoc { Cmd = "play", Path = remote })
                .GetAwaiter().GetResult();
            if (ok)
            {
                AnsiConsole.MarkupLineInterpolated($"  [green]playing[/] {remote}");
            }
            else
            {
                AnsiConsole.MarkupLine("  [red]device rejected the clip[/] (missing, or not 16 kHz/16-bit/mono)");
            }
        }
        catch (LaserTagApiException ex)
        {
            AnsiConsole.MarkupLineInterpolated($"  [red]play failed:[/] {ex.Message}");
        }
    }

    private void SdStartup(string ip, string? remote)
    {
        if (remote is null)
        {
            AnsiConsole.MarkupLine("[yellow]usage: sd startup <id> <remote|none>[/]");
            return;
        }

        // "none" is the spelling the CLI accepts for "silent at boot"; the
        // device stores an empty string.
        string value = remote.Equals("none", StringComparison.OrdinalIgnoreCase) ? string.Empty : remote;
        using var http = new HttpClient { BaseAddress = new Uri($"http://{ip}"), Timeout = TimeSpan.FromSeconds(10) };
        var client = new LaserTagClient(http);
        try
        {
            client.PatchConfigAsync(new Dictionary<string, object?> { ["startupSfx"] = value })
                .GetAwaiter().GetResult();
            if (value.Length == 0)
            {
                AnsiConsole.MarkupLine("  [green]startup sound cleared[/] (silent at boot)");
            }
            else
            {
                AnsiConsole.MarkupLineInterpolated($"  [green]startup sound set[/] -> {value}");
            }
        }
        catch (LaserTagApiException ex)
        {
            AnsiConsole.MarkupLineInterpolated($"  [red]failed:[/] {ex.Message}");
        }
    }

    private void PrintDevices()
    {
        var table = new Table().AddColumns("id", "host", "ip", "team", "hp", "online");
        foreach (var e in game.Devices())
        {
            Heartbeat hb = e.LastHeartbeat;
            table.AddRow(e.Id, hb.Source, hb.Ip, Teams.Describe(hb.Team), hb.Hp.ToString(), e.Online ? "yes" : "[red]no[/]");
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
