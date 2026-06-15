using System.Text.Json.Serialization;

namespace LaserTag.Client.Models;

/// <summary>
/// Base type for all UDP messages that originate from a device and therefore
/// carry a leading hostname token (contract §1.1). The hostname is captured as
/// the <see cref="Source"/>.
/// </summary>
public abstract record UdpInboundMessage
{
    /// <summary>
    /// Gets the sending device's hostname — the first whitespace-delimited token
    /// of the on-wire packet (contract §1.1).
    /// </summary>
    public required string Source { get; init; }
}

/// <summary>
/// A device discovery/liveness heartbeat (UDP class <c>HB</c>, contract §1.4).
/// Broadcast roughly every 2000 ms while the device is online.
/// </summary>
public sealed record Heartbeat : UdpInboundMessage
{
    /// <summary>Gets the device id (MAC-derived, stable, lowercase hex).</summary>
    public required string Id { get; init; }

    /// <summary>Gets the device's current IP address (dotted-quad).</summary>
    public required string Ip { get; init; }

    /// <summary>Gets the firmware version (semver, e.g. <c>2.0.0</c>).</summary>
    public required string Fw { get; init; }

    /// <summary>Gets the device's own team index (0..N).</summary>
    public required int Team { get; init; }

    /// <summary>Gets the active mode id (e.g. <c>team-colours</c>, <c>idle</c>).</summary>
    public required string Mode { get; init; }

    /// <summary>Gets the device's current health (0..100).</summary>
    public required int Hp { get; init; }

    /// <summary>
    /// Gets a value indicating whether the device reports itself online. Parsed
    /// from <c>online=1</c>; heartbeats are only sent while online.
    /// </summary>
    public required bool Online { get; init; }
}

/// <summary>
/// A telemetry hit event (UDP class <c>EVT hit</c>, contract §1.4) emitted by a
/// device after it decodes a valid shot and applies damage locally.
/// </summary>
public sealed record HitEvent : UdpInboundMessage
{
    /// <summary>Gets the device id of the hit (victim) device.</summary>
    public required string Victim { get; init; }

    /// <summary>Gets the firing team index.</summary>
    public required int ShooterTeam { get; init; }

    /// <summary>Gets the damage applied (1..4).</summary>
    public required int Dmg { get; init; }

    /// <summary>Gets the protocol id that decoded the shot (e.g. <c>vatos</c>).</summary>
    public required string Proto { get; init; }

    /// <summary>Gets the victim's resulting health after the hit (0..100).</summary>
    public required int Hp { get; init; }

    /// <summary>
    /// Gets the device <c>millis()</c> timestamp in milliseconds. Stored as a
    /// 64-bit value because the device timestamp is an unsigned 32-bit counter
    /// that exceeds <see cref="int"/> range near wrap-around.
    /// </summary>
    public required long Ts { get; init; }
}

/// <summary>
/// A telemetry state-change event (UDP class <c>EVT state</c>, contract §1.4),
/// e.g. a device entering the <c>dead</c> or <c>respawn</c> state.
/// </summary>
public sealed record StateEvent : UdpInboundMessage
{
    /// <summary>
    /// Gets the state value: one of <c>ready</c>, <c>idle</c>, <c>dead</c>,
    /// <c>respawn</c>.
    /// </summary>
    public required string S { get; init; }

    /// <summary>
    /// Gets the optional health at the time of the state change (0..100), or
    /// <see langword="null"/> when the <c>hp</c> key is absent.
    /// </summary>
    public int? Hp { get; init; }

    /// <summary>
    /// Gets the device <c>millis()</c> timestamp in milliseconds (see
    /// <see cref="HitEvent.Ts"/> for the rationale behind the 64-bit type).
    /// </summary>
    public required long Ts { get; init; }
}

/// <summary>
/// The kind of a host→device low-latency control message (contract §1.4).
/// </summary>
public enum ControlKind
{
    /// <summary>Simultaneous game start (<c>CTL start</c>).</summary>
    Start,

    /// <summary>Game stop (<c>CTL stop</c>).</summary>
    Stop,

    /// <summary>Force all devices to a state (<c>CTL reset</c>).</summary>
    Reset,
}

/// <summary>
/// A host→broadcast low-latency control message (UDP class <c>CTL</c>,
/// contract §1.4). Unlike inbound messages this carries NO hostname prefix on
/// the wire; the host formats it beginning at <c>CTL</c>.
/// </summary>
public sealed record Control
{
    /// <summary>Gets the control kind (<c>start</c>, <c>stop</c>, <c>reset</c>).</summary>
    public required ControlKind Kind { get; init; }

    /// <summary>
    /// Gets the optional <c>ts</c> timestamp (ms). Valid for <see cref="ControlKind.Start"/>.
    /// </summary>
    public long? Ts { get; init; }

    /// <summary>
    /// Gets the optional <c>hp</c> value. Valid for <see cref="ControlKind.Reset"/>.
    /// </summary>
    public int? Hp { get; init; }
}
