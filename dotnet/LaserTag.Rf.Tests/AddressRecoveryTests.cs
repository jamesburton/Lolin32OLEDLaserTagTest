namespace LaserTag.Rf.Tests;

public class AddressRecoveryTests
{
    private static RfCapture Capture(params byte[] data) => new(76, "1m", 0, data);

    [Fact]
    public void FindCandidates_PlantedAddress_RanksItFirst()
    {
        // Five captures share a planted 5-byte address; the noise around it
        // differs each time, so only the address should recur.
        byte[] address = [0xE7, 0xE7, 0xE7, 0xE7, 0xE7];
        var captures = new List<RfCapture>();
        for (byte i = 0; i < 5; ++i)
        {
            captures.Add(Capture([.. address, (byte)(0x10 + i), (byte)(0x20 + i), (byte)(0x30 + i)]));
        }

        IReadOnlyList<AddressCandidate> candidates = AddressRecovery.FindCandidates(captures);

        Assert.Equal(address, candidates[0].Address);
        Assert.Equal(5, candidates[0].Occurrences);
    }

    [Fact]
    public void FindCandidates_BelowMinOccurrences_ReturnsEmpty()
    {
        // Pure noise must yield nothing rather than a confident wrong answer.
        var captures = new List<RfCapture>
        {
            Capture(0x01, 0x02, 0x03, 0x04, 0x05, 0x06),
            Capture(0x11, 0x12, 0x13, 0x14, 0x15, 0x16),
        };

        Assert.Empty(AddressRecovery.FindCandidates(captures));
    }
}
