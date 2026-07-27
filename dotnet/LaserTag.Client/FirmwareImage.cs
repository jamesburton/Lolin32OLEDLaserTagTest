namespace LaserTag.Client;

/// <summary>
/// Verdict from comparing a device's running firmware version against the
/// available built image (fleet-ota spec).
/// </summary>
public enum FirmwareVerdict
{
    /// <summary>Either version was missing or unparseable.</summary>
    Unknown,

    /// <summary>The device runs the available version.</summary>
    Current,

    /// <summary>The device runs an older version — an OTA is due.</summary>
    Outdated,

    /// <summary>The device runs a NEWER version than the local image.</summary>
    Newer,
}

/// <summary>
/// Reads the firmware version embedded in a built image. The firmware embeds
/// an <c>LTFW:&lt;semver&gt;</c> marker string (see <c>kFwMarker</c> in
/// <c>matrix_main.cpp</c>) precisely so tooling can identify a .bin without
/// relying on the ESP-IDF app descriptor, which the precompiled Arduino core
/// controls.
/// </summary>
public static class FirmwareImage
{
    private static readonly byte[] Marker = "LTFW:"u8.ToArray();
    private const int MaxVersionLength = 24;

    /// <summary>Scans a firmware image for the embedded version marker.</summary>
    /// <param name="path">Path to the firmware .bin.</param>
    /// <returns>The version string (e.g. <c>2.1.0</c>), or <see langword="null"/>
    /// if the file is unreadable or carries no marker.</returns>
    public static string? TryReadVersion(string path)
    {
        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(path);
        }
        catch (IOException)
        {
            return null;
        }
        catch (UnauthorizedAccessException)
        {
            return null;
        }

        int at = bytes.AsSpan().IndexOf(Marker);
        if (at < 0)
        {
            return null;
        }

        int start = at + Marker.Length;
        int end = start;
        while (end < bytes.Length && end - start < MaxVersionLength &&
               bytes[end] > 0x20 && bytes[end] < 0x7F)
        {
            end++;
        }

        return end > start ? System.Text.Encoding.ASCII.GetString(bytes, start, end - start) : null;
    }

    /// <summary>Compares a device's running version to the available image version.</summary>
    /// <param name="running">The device-reported version (heartbeat <c>fw=</c>).</param>
    /// <param name="available">The image version, or <see langword="null"/> when unknown.</param>
    /// <returns>The comparison verdict.</returns>
    public static FirmwareVerdict Compare(string? running, string? available)
    {
        if (!Version.TryParse(running, out Version? dev) || !Version.TryParse(available, out Version? img))
        {
            return FirmwareVerdict.Unknown;
        }

        int cmp = dev.CompareTo(img);
        return cmp == 0 ? FirmwareVerdict.Current
            : cmp < 0 ? FirmwareVerdict.Outdated
            : FirmwareVerdict.Newer;
    }
}
