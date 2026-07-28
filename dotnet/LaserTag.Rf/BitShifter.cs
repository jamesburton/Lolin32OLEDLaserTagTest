namespace LaserTag.Rf;

/// <summary>
/// Bit-level realignment for promiscuous captures.
/// </summary>
/// <remarks>
/// The 2-byte pseudo-address trick can latch a packet one to seven bit
/// positions off, so every candidate must be tried at all eight offsets before
/// concluding it is noise.
/// </remarks>
public static class BitShifter
{
    /// <summary>
    /// Shifts the buffer left by the given number of bits, zero-filling the tail.
    /// </summary>
    /// <param name="data">The bytes to shift.</param>
    /// <param name="bits">How many bits to shift left (0-7).</param>
    /// <returns>A new, shifted buffer of the same length.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// Thrown when <paramref name="bits"/> is outside 0-7.
    /// </exception>
    public static byte[] ShiftLeft(ReadOnlySpan<byte> data, int bits)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(bits);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(bits, 7);

        var result = new byte[data.Length];
        for (int i = 0; i < data.Length; ++i)
        {
            // Shifting a byte right by 8 is a no-op in C# (the shift count is
            // masked to 5 bits), so the zero-shift case must be handled apart.
            if (bits == 0)
            {
                result[i] = data[i];
                continue;
            }

            int high = data[i] << bits;
            int low = i + 1 < data.Length ? data[i + 1] >> (8 - bits) : 0;
            result[i] = (byte)(high | low);
        }

        return result;
    }
}
