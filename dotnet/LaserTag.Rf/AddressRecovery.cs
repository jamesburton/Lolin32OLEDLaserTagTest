namespace LaserTag.Rf;

/// <summary>
/// A repeated byte sequence that may be a real nRF24 pipe address.
/// </summary>
/// <param name="Address">The candidate address bytes.</param>
/// <param name="Occurrences">How many captures contained it.</param>
public record AddressCandidate(byte[] Address, int Occurrences);

/// <summary>
/// Recovers real pipe addresses from promiscuous-mode captures.
/// </summary>
/// <remarks>
/// With the address width set illegally short, the target's real address lands
/// inside the payload. It is the one sequence that repeats across otherwise
/// unrelated captures, so frequency counting surfaces it.
/// </remarks>
public static class AddressRecovery
{
    /// <summary>
    /// Ranks repeated byte sequences by how many captures contain them.
    /// </summary>
    /// <param name="captures">Captures to analyse, at any bit alignment.</param>
    /// <param name="addressLength">Address width in bytes (nRF24 uses 3-5).</param>
    /// <param name="minOccurrences">
    /// Discard sequences seen in fewer captures than this. Promiscuous capture
    /// is roughly 19 parts noise to 1 part signal, so a low floor produces
    /// confident nonsense.
    /// </param>
    /// <returns>Candidates, most frequent first.</returns>
    public static IReadOnlyList<AddressCandidate> FindCandidates(
        IEnumerable<RfCapture> captures,
        int addressLength = 5,
        int minOccurrences = 3)
    {
        var counts = new Dictionary<string, (byte[] Address, int Count)>();
        foreach (RfCapture capture in captures)
        {
            // Count each distinct sequence once per capture: a sequence that
            // repeats inside one noisy packet is not corroborating evidence.
            var seenHere = new HashSet<string>();
            for (int offset = 0; offset + addressLength <= capture.Data.Length; ++offset)
            {
                byte[] slice = capture.Data[offset..(offset + addressLength)];
                string key = Convert.ToHexString(slice);
                if (!seenHere.Add(key))
                {
                    continue;
                }

                counts[key] = counts.TryGetValue(key, out var existing)
                    ? (existing.Address, existing.Count + 1)
                    : (slice, 1);
            }
        }

        return counts.Values
            .Where(v => v.Count >= minOccurrences)
            .OrderByDescending(v => v.Count)
            .ThenBy(v => Convert.ToHexString(v.Address), StringComparer.Ordinal)
            .Select(v => new AddressCandidate(v.Address, v.Count))
            .ToList();
    }
}
