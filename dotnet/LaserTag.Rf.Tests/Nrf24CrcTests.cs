namespace LaserTag.Rf.Tests;

public class Nrf24CrcTests
{
    [Fact]
    public void Compute_KnownVector_MatchesCcittFalse()
    {
        // CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over "123456789" is
        // 0x29B1 - the standard check value, which pins the polynomial and
        // initial value independently of any nRF24 capture.
        byte[] data = "123456789"u8.ToArray();

        Assert.Equal(0x29B1, Nrf24Crc.Compute(data, data.Length * 8));
    }

    [Fact]
    public void Compute_PartialBitLength_IgnoresTrailingBits()
    {
        // nRF24 CRCs cover a non-byte-aligned bit range (address + control +
        // payload), so trailing bits beyond bitLength must not contribute.
        byte[] full = [0xAB, 0xFF];
        byte[] masked = [0xAB, 0x00];

        Assert.Equal(Nrf24Crc.Compute(full, 8), Nrf24Crc.Compute(masked, 8));
    }
}
