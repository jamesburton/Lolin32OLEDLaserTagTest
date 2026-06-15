using System.Text.Json.Serialization;

namespace LaserTag.Client.Models;

/// <summary>
/// Live runtime status returned by <c>GET /api/status</c> (contract §2.2).
/// </summary>
public sealed record StatusDoc
{
    /// <summary>Gets the stable device id (MAC-derived, lowercase hex).</summary>
    [JsonPropertyName("deviceId")]
    public required string DeviceId { get; init; }

    /// <summary>Gets the device hostname (OTA / mDNS name).</summary>
    [JsonPropertyName("hostname")]
    public required string Hostname { get; init; }

    /// <summary>Gets the firmware version (semver).</summary>
    [JsonPropertyName("fw")]
    public required string Fw { get; init; }

    /// <summary>Gets the active mode id.</summary>
    [JsonPropertyName("mode")]
    public required string Mode { get; init; }

    /// <summary>Gets the device's own team index.</summary>
    [JsonPropertyName("ownTeam")]
    public required int OwnTeam { get; init; }

    /// <summary>Gets the current health (0..100).</summary>
    [JsonPropertyName("hp")]
    public required int Hp { get; init; }

    /// <summary>Gets a value indicating whether the device is online.</summary>
    [JsonPropertyName("online")]
    public required bool Online { get; init; }

    /// <summary>Gets the device uptime in milliseconds.</summary>
    [JsonPropertyName("uptimeMs")]
    public required long UptimeMs { get; init; }

    /// <summary>Gets the current WiFi RSSI (dBm).</summary>
    [JsonPropertyName("rssi")]
    public required int Rssi { get; init; }
}

/// <summary>
/// Full persisted configuration (the NVS fields) returned by
/// <c>GET /api/config</c> and <c>PATCH /api/config</c> (contract §2.2).
/// </summary>
public sealed record ConfigDoc
{
    /// <summary>Gets the stable device id.</summary>
    [JsonPropertyName("deviceId")]
    public required string DeviceId { get; init; }

    /// <summary>Gets the device hostname.</summary>
    [JsonPropertyName("hostname")]
    public required string Hostname { get; init; }

    /// <summary>Gets the device's own team index.</summary>
    [JsonPropertyName("ownTeam")]
    public required int OwnTeam { get; init; }

    /// <summary>Gets the active subset of team indices.</summary>
    [JsonPropertyName("enabledTeams")]
    public required IReadOnlyList<int> EnabledTeams { get; init; }

    /// <summary>Gets the protocol id (e.g. <c>vatos</c>).</summary>
    [JsonPropertyName("protocolId")]
    public required string ProtocolId { get; init; }

    /// <summary>Gets the LED brightness.</summary>
    [JsonPropertyName("brightness")]
    public required int Brightness { get; init; }

    /// <summary>
    /// Gets the team colour override map. JSON keys are team indices as strings;
    /// values are <c>#RRGGBB</c>. System.Text.Json converts the string keys to
    /// the <see cref="int"/> dictionary keys automatically.
    /// </summary>
    [JsonPropertyName("teamColours")]
    public required IReadOnlyDictionary<int, string> TeamColours { get; init; }
}

/// <summary>
/// Mode timing parameters carried inside a <see cref="ModeDoc"/> (contract §2.2).
/// Only the keys present in a request are serialized.
/// </summary>
public sealed record ModeTimings
{
    /// <summary>Gets the minimum "dark" delay in milliseconds.</summary>
    [JsonPropertyName("darkMinMs")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? DarkMinMs { get; init; }

    /// <summary>Gets the maximum "dark" delay in milliseconds.</summary>
    [JsonPropertyName("darkMaxMs")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? DarkMaxMs { get; init; }
}

/// <summary>
/// Runtime mode selection for <c>POST /api/mode</c> (contract §2.2). Not
/// persisted; echoed back on success.
/// </summary>
public sealed record ModeDoc
{
    /// <summary>Gets the mode id to activate.</summary>
    [JsonPropertyName("mode")]
    public required string Mode { get; init; }

    /// <summary>Gets the optional mode timing parameters.</summary>
    [JsonPropertyName("timings")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public ModeTimings? Timings { get; init; }
}

/// <summary>
/// A structured one-shot command for <c>POST /api/command</c> (contract §2.2).
/// Supported <see cref="Cmd"/> values include <c>identify</c>, <c>bright</c>,
/// <c>hit</c> and <c>debug</c>. Only the keys relevant to a given command are
/// serialized; null optionals are omitted.
/// </summary>
public sealed record CommandDoc
{
    /// <summary>Gets the command name.</summary>
    [JsonPropertyName("cmd")]
    public required string Cmd { get; init; }

    /// <summary>Gets the optional scalar value (e.g. for <c>bright</c>, <c>debug</c>).</summary>
    [JsonPropertyName("value")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? Value { get; init; }

    /// <summary>Gets the optional team index (e.g. for a test <c>hit</c>).</summary>
    [JsonPropertyName("team")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? Team { get; init; }

    /// <summary>Gets the optional damage value (e.g. for a test <c>hit</c>).</summary>
    [JsonPropertyName("damage")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? Damage { get; init; }
}
