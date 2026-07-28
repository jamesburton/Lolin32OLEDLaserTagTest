namespace LaserTag.Rf;

/// <summary>
/// One promiscuous-mode capture emitted by the RF probe firmware.
/// </summary>
/// <param name="Channel">nRF24 channel 0-125 (2400 + channel MHz).</param>
/// <param name="Rate">Air data rate token: 250k, 1m or 2m.</param>
/// <param name="TimestampUs">Probe-side microsecond timestamp.</param>
/// <param name="Data">Raw captured bytes, not yet realigned or validated.</param>
public record RfCapture(int Channel, string Rate, long TimestampUs, byte[] Data);
