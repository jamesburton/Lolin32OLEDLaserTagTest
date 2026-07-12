using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Linq;

namespace LaserTag.Client;

/// <summary>
/// Derives the IPv4 subnet-directed broadcast address (devices do not receive
/// <c>255.255.255.255</c>; CTL must target e.g. <c>192.168.1.255</c>).
/// </summary>
public static class BroadcastAddress
{
    /// <summary>
    /// Computes the subnet broadcast address for an IPv4 address + mask.
    /// </summary>
    /// <param name="address">A local IPv4 unicast address.</param>
    /// <param name="mask">The subnet mask for that address.</param>
    /// <returns>The subnet-directed broadcast address.</returns>
    public static IPAddress Compute(IPAddress address, IPAddress mask)
    {
        ArgumentNullException.ThrowIfNull(address);
        ArgumentNullException.ThrowIfNull(mask);
        byte[] addr = address.GetAddressBytes();
        byte[] m = mask.GetAddressBytes();
        var result = new byte[addr.Length];
        for (int i = 0; i < addr.Length; i++)
        {
            result[i] = (byte)(addr[i] | ~m[i]);
        }

        return new IPAddress(result);
    }

    /// <summary>
    /// Determines whether an IPv4 address falls within one of the RFC 1918
    /// private address ranges (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16).
    /// </summary>
    /// <param name="address">The address to test.</param>
    /// <returns>
    /// <see langword="true"/> if <paramref name="address"/> is an IPv4 address in
    /// one of the RFC 1918 private ranges; otherwise <see langword="false"/>
    /// (including for non-IPv4 addresses).
    /// </returns>
    public static bool IsRfc1918(IPAddress address)
    {
        ArgumentNullException.ThrowIfNull(address);

        if (address.AddressFamily != AddressFamily.InterNetwork)
        {
            return false;
        }

        byte[] b = address.GetAddressBytes();

        // 10.0.0.0/8
        if (b[0] == 10)
        {
            return true;
        }

        // 172.16.0.0/12 (172.16.0.0 - 172.31.255.255)
        if (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
        {
            return true;
        }

        // 192.168.0.0/16
        if (b[0] == 192 && b[1] == 168)
        {
            return true;
        }

        return false;
    }

    /// <summary>
    /// Scans the machine's NICs for an operational IPv4 interface with an
    /// RFC 1918 private-range unicast address and returns its subnet broadcast
    /// endpoint. Physical NICs (Ethernet, Wi-Fi) are preferred: they are
    /// searched first, and only if none yield a suitable address are other
    /// up, non-loopback interface types (e.g. virtual adapters, tunnels)
    /// considered, still restricted to RFC 1918 addresses.
    /// </summary>
    /// <param name="port">The UDP port to pair with the broadcast address (4210 for CTL).</param>
    /// <returns>The endpoint, or <see langword="null"/> if no suitable NIC was found.</returns>
    public static IPEndPoint? DiscoverLocalBroadcastEndpoint(int port)
    {
        List<NetworkInterface> candidates = NetworkInterface.GetAllNetworkInterfaces()
            .Where(nic => nic.OperationalStatus == OperationalStatus.Up &&
                nic.NetworkInterfaceType != NetworkInterfaceType.Loopback)
            .ToList();

        IPEndPoint? physical = FindBroadcastEndpoint(
            candidates.Where(nic => nic.NetworkInterfaceType is NetworkInterfaceType.Ethernet
                or NetworkInterfaceType.Wireless80211),
            port);
        if (physical is not null)
        {
            return physical;
        }

        return FindBroadcastEndpoint(candidates, port);
    }

    /// <summary>
    /// Searches the given NICs' unicast addresses for the first RFC 1918
    /// IPv4 address and returns its subnet broadcast endpoint.
    /// </summary>
    /// <param name="nics">The network interfaces to search, in order.</param>
    /// <param name="port">The UDP port to pair with the broadcast address.</param>
    /// <returns>The endpoint, or <see langword="null"/> if none was found.</returns>
    private static IPEndPoint? FindBroadcastEndpoint(IEnumerable<NetworkInterface> nics, int port)
    {
        foreach (NetworkInterface nic in nics)
        {
            foreach (UnicastIPAddressInformation ua in nic.GetIPProperties().UnicastAddresses)
            {
                if (ua.Address.AddressFamily == AddressFamily.InterNetwork &&
                    ua.IPv4Mask is not null &&
                    IsRfc1918(ua.Address))
                {
                    return new IPEndPoint(Compute(ua.Address, ua.IPv4Mask), port);
                }
            }
        }

        return null;
    }
}
