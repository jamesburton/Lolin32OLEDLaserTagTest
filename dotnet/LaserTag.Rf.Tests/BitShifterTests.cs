namespace LaserTag.Rf.Tests;

public class BitShifterTests
{
    [Fact]
    public void ShiftLeft_ZeroBits_ReturnsCopy()
    {
        byte[] data = [0x12, 0x34];

        Assert.Equal(data, BitShifter.ShiftLeft(data, 0));
    }

    [Fact]
    public void ShiftLeft_OneBit_PullsInNextByteMsb()
    {
        // 0x80 0x01 = 1000_0000 0000_0001; shifted left one bit the second
        // byte's MSB (0) enters the first byte's LSB.
        Assert.Equal(new byte[] { 0x00, 0x02 }, BitShifter.ShiftLeft([0x80, 0x01], 1));
    }

    [Fact]
    public void ShiftLeft_SevenBits_RecoversMisalignedPacket()
    {
        // A capture that arrived one bit-position late: shifting realigns it.
        Assert.Equal(new byte[] { 0xFF, 0x80 }, BitShifter.ShiftLeft([0x01, 0xFF], 7));
    }
}
