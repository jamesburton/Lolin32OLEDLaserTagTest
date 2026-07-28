namespace LaserTag.Rf;

/// <summary>
/// CRC-16/CCITT as used by the nRF24L01+ (polynomial 0x1021, initial 0xFFFF).
/// </summary>
public static class Nrf24Crc
{
    private const ushort Polynomial = 0x1021;

    /// <summary>
    /// Computes the CRC over a bit range, MSB first.
    /// </summary>
    /// <param name="data">The bytes to cover.</param>
    /// <param name="bitLength">
    /// How many bits of <paramref name="data"/> participate. nRF24 packets are
    /// not byte-aligned, so this is a bit count rather than a byte count.
    /// </param>
    /// <returns>The 16-bit CRC.</returns>
    public static ushort Compute(ReadOnlySpan<byte> data, int bitLength)
    {
        ushort crc = 0xFFFF;
        for (int bit = 0; bit < bitLength; ++bit)
        {
            int value = (data[bit / 8] >> (7 - (bit % 8))) & 1;
            bool xorNeeded = ((crc >> 15) & 1) != value;
            crc <<= 1;
            if (xorNeeded)
            {
                crc ^= Polynomial;
            }
        }

        return crc;
    }
}
