using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

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
    /// Scans the machine's NICs for the first operational IPv4 interface with a
    /// private-range unicast address and returns its subnet broadcast endpoint.
    /// </summary>
    /// <param name="port">The UDP port to pair with the broadcast address (4210 for CTL).</param>
    /// <returns>The endpoint, or <see langword="null"/> if no suitable NIC was found.</returns>
    public static IPEndPoint? DiscoverLocalBroadcastEndpoint(int port)
    {
        foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (nic.OperationalStatus != OperationalStatus.Up ||
                nic.NetworkInterfaceType == NetworkInterfaceType.Loopback)
            {
                continue;
            }

            foreach (UnicastIPAddressInformation ua in nic.GetIPProperties().UnicastAddresses)
            {
                if (ua.Address.AddressFamily == AddressFamily.InterNetwork && ua.IPv4Mask is not null)
                {
                    return new IPEndPoint(Compute(ua.Address, ua.IPv4Mask), port);
                }
            }
        }

        return null;
    }
}
