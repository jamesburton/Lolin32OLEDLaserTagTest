using LaserTag.Client.Models;

namespace LaserTag.Client;

/// <summary>
/// A live entry in the <see cref="DeviceRoster"/>: the most recent heartbeat for
/// a device plus the derived liveness metadata (contract §4, design §6).
/// </summary>
public sealed record RosterEntry
{
    /// <summary>Gets the device id this entry is keyed by.</summary>
    public required string Id { get; init; }

    /// <summary>Gets the most recently ingested heartbeat for this device.</summary>
    public required Heartbeat LastHeartbeat { get; init; }

    /// <summary>Gets the time the last heartbeat was ingested (per the injected clock).</summary>
    public required DateTimeOffset LastSeen { get; init; }

    /// <summary>
    /// Gets a value indicating whether the device is currently considered online
    /// (i.e. it has been seen within the offline timeout window).
    /// </summary>
    public required bool Online { get; init; }

    /// <summary>
    /// Gets a value indicating whether the device rejoined: it sent a heartbeat
    /// after previously having been marked offline (design §6). The flag is
    /// sticky until cleared via <see cref="DeviceRoster.ClearRejoined"/> or until
    /// the device goes offline again. It is never set on a device's very first
    /// heartbeat.
    /// </summary>
    public required bool Rejoined { get; init; }
}

/// <summary>
/// Ingests parsed <see cref="Heartbeat"/> messages into a live roster keyed by
/// device id, deriving online/offline liveness against an injectable clock and
/// detecting devices that rejoin after going offline (design §6, contract §4).
/// </summary>
/// <remarks>
/// All liveness is computed against the supplied time source — no real
/// <c>DateTime.Now</c> is read inside the logic — so tests can advance the clock
/// deterministically. The offline timeout defaults to 6000 ms (3 missed 2000 ms
/// beats) per contract §4.
/// </remarks>
public sealed class DeviceRoster
{
    /// <summary>The default offline timeout (contract §4: 3 missed 2000 ms beats).</summary>
    public static readonly TimeSpan DefaultOfflineTimeout = TimeSpan.FromMilliseconds(6000);

    private readonly Func<DateTimeOffset> _clock;
    private readonly TimeSpan _offlineTimeout;
    private readonly Dictionary<string, MutableEntry> _entries = new(StringComparer.Ordinal);

    /// <summary>
    /// Initializes a new instance of the <see cref="DeviceRoster"/> class.
    /// </summary>
    /// <param name="clock">
    /// The injectable time source used for all liveness calculations. Tests pass
    /// a controllable clock; production typically passes
    /// <c>() =&gt; DateTimeOffset.UtcNow</c>.
    /// </param>
    /// <param name="offlineTimeout">
    /// The duration without a heartbeat after which a device is marked offline.
    /// Defaults to <see cref="DefaultOfflineTimeout"/> (6000 ms) when omitted.
    /// </param>
    /// <exception cref="ArgumentNullException">Thrown if <paramref name="clock"/> is null.</exception>
    public DeviceRoster(Func<DateTimeOffset> clock, TimeSpan? offlineTimeout = null)
    {
        ArgumentNullException.ThrowIfNull(clock);
        _clock = clock;
        _offlineTimeout = offlineTimeout ?? DefaultOfflineTimeout;
    }

    /// <summary>
    /// Ingests a heartbeat, registering a new device or refreshing an existing
    /// one. If the device was previously offline this marks it as rejoined.
    /// </summary>
    /// <param name="heartbeat">The parsed heartbeat to ingest.</param>
    /// <returns>The resulting roster entry for the device.</returns>
    /// <exception cref="ArgumentNullException">Thrown if <paramref name="heartbeat"/> is null.</exception>
    public RosterEntry Ingest(Heartbeat heartbeat)
    {
        ArgumentNullException.ThrowIfNull(heartbeat);

        DateTimeOffset now = _clock();

        if (_entries.TryGetValue(heartbeat.Id, out MutableEntry? existing))
        {
            // A device whose last heartbeat is older than the timeout was
            // effectively offline; a fresh beat means it has rejoined.
            bool wasOffline = now - existing.LastSeen > _offlineTimeout;
            existing.LastHeartbeat = heartbeat;
            existing.LastSeen = now;
            if (wasOffline)
            {
                existing.Rejoined = true;
            }

            return Snapshot(existing, now);
        }

        var entry = new MutableEntry
        {
            Id = heartbeat.Id,
            LastHeartbeat = heartbeat,
            LastSeen = now,
            Rejoined = false,
        };
        _entries[heartbeat.Id] = entry;
        return Snapshot(entry, now);
    }

    /// <summary>
    /// Gets the current entry for a device, with online state evaluated against
    /// the clock at call time.
    /// </summary>
    /// <param name="id">The device id.</param>
    /// <returns>The roster entry, or <see langword="null"/> if the device is unknown.</returns>
    public RosterEntry? Get(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        return _entries.TryGetValue(id, out MutableEntry? entry)
            ? Snapshot(entry, _clock())
            : null;
    }

    /// <summary>
    /// Gets a snapshot of all known devices, with online state evaluated against
    /// the clock at call time.
    /// </summary>
    /// <returns>A read-only list of roster entries.</returns>
    public IReadOnlyList<RosterEntry> GetAll()
    {
        DateTimeOffset now = _clock();
        var result = new List<RosterEntry>(_entries.Count);
        foreach (MutableEntry entry in _entries.Values)
        {
            result.Add(Snapshot(entry, now));
        }

        return result;
    }

    /// <summary>
    /// Determines whether a device is currently online (seen within the offline
    /// timeout window), evaluated against the clock at call time.
    /// </summary>
    /// <param name="id">The device id.</param>
    /// <returns>
    /// <see langword="true"/> if the device is known and online; otherwise
    /// <see langword="false"/> (including for unknown devices).
    /// </returns>
    public bool IsOnline(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        return _entries.TryGetValue(id, out MutableEntry? entry) && IsOnlineAt(entry, _clock());
    }

    /// <summary>
    /// Clears the rejoined flag for a device (e.g. after the operator has
    /// acknowledged the rejoin).
    /// </summary>
    /// <param name="id">The device id.</param>
    /// <returns>
    /// <see langword="true"/> if the device was known and its flag cleared;
    /// otherwise <see langword="false"/>.
    /// </returns>
    public bool ClearRejoined(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        if (_entries.TryGetValue(id, out MutableEntry? entry))
        {
            entry.Rejoined = false;
            return true;
        }

        return false;
    }

    private bool IsOnlineAt(MutableEntry entry, DateTimeOffset now) =>
        now - entry.LastSeen <= _offlineTimeout;

    private RosterEntry Snapshot(MutableEntry entry, DateTimeOffset now)
    {
        bool online = IsOnlineAt(entry, now);

        // A device that has aged out is no longer "rejoined"; the flag describes
        // a currently-present device that came back.
        bool rejoined = entry.Rejoined && online;

        return new RosterEntry
        {
            Id = entry.Id,
            LastHeartbeat = entry.LastHeartbeat,
            LastSeen = entry.LastSeen,
            Online = online,
            Rejoined = rejoined,
        };
    }

    /// <summary>Internal mutable backing store for a roster entry.</summary>
    private sealed class MutableEntry
    {
        public required string Id { get; init; }

        public required Heartbeat LastHeartbeat { get; set; }

        public required DateTimeOffset LastSeen { get; set; }

        public required bool Rejoined { get; set; }
    }
}
