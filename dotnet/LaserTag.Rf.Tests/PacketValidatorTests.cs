namespace LaserTag.Rf.Tests;

public class PacketValidatorTests
{
    /// <summary>
    /// Builds a valid Enhanced ShockBurst packet: address, then a 9-bit packet
    /// control field (6-bit length, 2-bit PID, 1-bit no-ack), then payload,
    /// then CRC16 over everything from the address onward.
    /// </summary>
    private static byte[] BuildPacket(byte[] address, byte[] payload, int pid, bool noAck)
    {
        var bits = new List<int>();
        void PushByte(byte b)
        {
            for (int i = 7; i >= 0; --i)
            {
                bits.Add((b >> i) & 1);
            }
        }

        foreach (byte b in address)
        {
            PushByte(b);
        }

        for (int i = 5; i >= 0; --i)
        {
            bits.Add((payload.Length >> i) & 1);
        }

        bits.Add((pid >> 1) & 1);
        bits.Add(pid & 1);
        bits.Add(noAck ? 1 : 0);
        foreach (byte b in payload)
        {
            PushByte(b);
        }

        byte[] covered = new byte[(bits.Count + 7) / 8];
        for (int i = 0; i < bits.Count; ++i)
        {
            covered[i / 8] |= (byte)(bits[i] << (7 - (i % 8)));
        }

        ushort crc = Nrf24Crc.Compute(covered, bits.Count);
        for (int i = 15; i >= 0; --i)
        {
            bits.Add((crc >> i) & 1);
        }

        byte[] packet = new byte[(bits.Count + 7) / 8];
        for (int i = 0; i < bits.Count; ++i)
        {
            packet[i / 8] |= (byte)(bits[i] << (7 - (i % 8)));
        }

        return packet;
    }

    [Fact]
    public void TryValidate_WellFormedPacket_RecoversFields()
    {
        byte[] address = [0xE7, 0xE7, 0xE7, 0xE7, 0xE7];
        byte[] payload = [0xDE, 0xAD, 0xBE, 0xEF];

        bool ok = PacketValidator.TryValidate(
            BuildPacket(address, payload, pid: 2, noAck: false), 5, out ValidatedPacket packet);

        Assert.True(ok);
        Assert.Equal(address, packet.Address);
        Assert.Equal(payload, packet.Payload);
        Assert.Equal(2, packet.Pid);
        Assert.False(packet.NoAck);
        Assert.Equal(0, packet.BitShift);
    }

    [Fact]
    public void TryValidate_BitShiftedPacket_RealignsAndRecovers()
    {
        // Promiscuous capture routinely latches a packet a few bit positions
        // late; the validator must try every offset before giving up.
        byte[] address = [0x11, 0x22, 0x33, 0x44, 0x55];
        byte[] payload = [0x01, 0x02];
        byte[] packet = BuildPacket(address, payload, pid: 1, noAck: true);
        byte[] shifted = new byte[packet.Length + 1];
        for (int i = 0; i < packet.Length; ++i)
        {
            shifted[i] |= (byte)(packet[i] >> 3);
            shifted[i + 1] = (byte)(packet[i] << 5);
        }

        bool ok = PacketValidator.TryValidate(shifted, 5, out ValidatedPacket recovered);

        Assert.True(ok);
        Assert.Equal(address, recovered.Address);
        Assert.Equal(payload, recovered.Payload);
        Assert.Equal(3, recovered.BitShift);
    }

    [Fact]
    public void TryValidate_Noise_ReturnsFalse()
    {
        // ~19 of every 20 promiscuous captures are noise. Rejecting them is the
        // validator's main job, so a false positive here matters more than a miss.
        byte[] noise = [0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55];

        Assert.False(PacketValidator.TryValidate(noise, 5, out _));
    }
}
