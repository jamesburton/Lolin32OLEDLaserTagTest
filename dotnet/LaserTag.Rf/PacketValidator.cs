namespace LaserTag.Rf;

/// <summary>
/// A capture that survived CRC validation as an Enhanced ShockBurst packet.
/// </summary>
/// <param name="Address">The recovered pipe address.</param>
/// <param name="Pid">The 2-bit packet identifier (sequence counter).</param>
/// <param name="NoAck">True when the no-acknowledge flag was set.</param>
/// <param name="Payload">The payload bytes.</param>
/// <param name="BitShift">Bit offset at which the packet was found (0-7).</param>
public record ValidatedPacket(byte[] Address, int Pid, bool NoAck, byte[] Payload, int BitShift);

/// <summary>
/// Separates genuine nRF24 packets from promiscuous-capture noise.
/// </summary>
/// <remarks>
/// An ESB packet is address, then a 9-bit packet control field, then payload,
/// then a CRC16 over everything from the address onward. Because the control
/// field is 9 bits the payload is not byte-aligned, which is why validation
/// works on bit offsets rather than bytes. Recomputing the CRC is the only
/// reliable way to tell a real packet from the roughly 19-in-20 that are noise.
/// </remarks>
public static class PacketValidator
{
    private const int ControlFieldBits = 9;
    private const int CrcBits = 16;

    /// <summary>
    /// Attempts to interpret a raw capture as a valid ESB packet at any bit offset.
    /// </summary>
    /// <param name="data">Raw captured bytes.</param>
    /// <param name="addressLength">Expected address width in bytes (3-5).</param>
    /// <param name="packet">The recovered packet when this returns true.</param>
    /// <returns>True if some bit offset yields a CRC-valid packet.</returns>
    public static bool TryValidate(ReadOnlySpan<byte> data, int addressLength, out ValidatedPacket packet)
    {
        packet = default!;
        int addressBits = addressLength * 8;
        int available = data.Length * 8;
        if (available < addressBits + ControlFieldBits + CrcBits)
        {
            return false; // Too short at any offset.
        }

        for (int shift = 0; shift < 8; ++shift)
        {
            byte[] aligned = BitShifter.ShiftLeft(data, shift);
            int payloadLength = (int)ReadBits(aligned, addressBits, 6);
            int coveredBits = addressBits + ControlFieldBits + (payloadLength * 8);
            if (payloadLength == 0 || coveredBits + CrcBits > available)
            {
                continue;
            }

            ushort expected = (ushort)ReadBits(aligned, coveredBits, CrcBits);
            if (Nrf24Crc.Compute(aligned, coveredBits) != expected)
            {
                continue;
            }

            var payload = new byte[payloadLength];
            for (int i = 0; i < payloadLength; ++i)
            {
                payload[i] = (byte)ReadBits(aligned, addressBits + ControlFieldBits + (i * 8), 8);
            }

            packet = new ValidatedPacket(
                aligned[..addressLength],
                (int)ReadBits(aligned, addressBits + 6, 2),
                ReadBits(aligned, addressBits + 8, 1) == 1,
                payload,
                shift);
            return true;
        }

        return false;
    }

    /// <summary>Reads up to 32 bits, MSB first, from an arbitrary bit offset.</summary>
    private static uint ReadBits(ReadOnlySpan<byte> data, int bitOffset, int count)
    {
        uint value = 0;
        for (int i = 0; i < count; ++i)
        {
            int bit = bitOffset + i;
            value = (value << 1) | (uint)((data[bit / 8] >> (7 - (bit % 8))) & 1);
        }

        return value;
    }
}
