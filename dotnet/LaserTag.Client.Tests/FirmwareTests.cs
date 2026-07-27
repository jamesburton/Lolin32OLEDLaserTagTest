using System.Net;
using System.Text;

namespace LaserTag.Client.Tests;

/// <summary>Tests for <see cref="FirmwareImage"/> marker scanning.</summary>
public class FirmwareImageTests
{
    private static string WriteTemp(byte[] bytes)
    {
        string path = Path.Combine(Path.GetTempPath(), $"ltfw-{Guid.NewGuid():N}.bin");
        File.WriteAllBytes(path, bytes);
        return path;
    }

    [Fact]
    public void ReadsVersion_FromMarkerMidFile()
    {
        byte[] blob = [.. new byte[512], .. Encoding.ASCII.GetBytes("LTFW:2.1.0\0"), .. new byte[256]];
        string path = WriteTemp(blob);
        try
        {
            Assert.Equal("2.1.0", FirmwareImage.TryReadVersion(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void ReturnsNull_WhenMarkerMissing()
    {
        string path = WriteTemp(new byte[1024]);
        try
        {
            Assert.Null(FirmwareImage.TryReadVersion(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void StopsAtNonPrintable_AndCapsLength()
    {
        // Marker followed by garbage instead of a terminator: version stops at
        // the first non-printable byte.
        byte[] blob = [.. Encoding.ASCII.GetBytes("xxLTFW:9.9.9"), 0x01, .. Encoding.ASCII.GetBytes("junk")];
        string path = WriteTemp(blob);
        try
        {
            Assert.Equal("9.9.9", FirmwareImage.TryReadVersion(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void ReturnsNull_ForTruncatedMarkerAtEof()
    {
        string path = WriteTemp(Encoding.ASCII.GetBytes("padLTFW:"));
        try
        {
            Assert.Null(FirmwareImage.TryReadVersion(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Theory]
    [InlineData("2.0.0", "2.1.0", FirmwareVerdict.Outdated)]
    [InlineData("2.1.0", "2.1.0", FirmwareVerdict.Current)]
    [InlineData("2.2.0", "2.1.0", FirmwareVerdict.Newer)]
    [InlineData("garbage", "2.1.0", FirmwareVerdict.Unknown)]
    [InlineData("2.1.0", null, FirmwareVerdict.Unknown)]
    public void Compare_YieldsExpectedVerdict(string running, string? available, FirmwareVerdict expected)
    {
        Assert.Equal(expected, FirmwareImage.Compare(running, available));
    }
}

/// <summary>Tests for <see cref="FirmwareUpdater"/> against a loopback HTTP stub.</summary>
public class FirmwareUpdaterTests
{
    private static async Task<(FirmwareUpdater.Result Result, string? SeenContentType)> RunAgainstStub(
        int statusCode, string body)
    {
        // Loopback stub standing in for the device's /api/update.
        var listener = new HttpListener();
        int port = Random.Shared.Next(20000, 60000);
        listener.Prefixes.Add($"http://127.0.0.1:{port}/");
        listener.Start();
        string? seenContentType = null;
        Task serve = Task.Run(async () =>
        {
            HttpListenerContext ctx = await listener.GetContextAsync();
            seenContentType = ctx.Request.ContentType;
            using var _ = new StreamReader(ctx.Request.InputStream);
            await _.ReadToEndAsync(); // drain the upload
            byte[] payload = Encoding.UTF8.GetBytes(body);
            ctx.Response.StatusCode = statusCode;
            await ctx.Response.OutputStream.WriteAsync(payload);
            ctx.Response.Close();
        });

        string bin = Path.Combine(Path.GetTempPath(), $"ltfw-up-{Guid.NewGuid():N}.bin");
        File.WriteAllBytes(bin, Encoding.ASCII.GetBytes("LTFW:2.1.0\0fake-image"));
        try
        {
            var updater = new FirmwareUpdater(TimeSpan.FromSeconds(10));
            FirmwareUpdater.Result result = await updater.UploadAsync($"127.0.0.1:{port}", bin, CancellationToken.None);
            await serve;
            return (result, seenContentType);
        }
        finally
        {
            listener.Stop();
            File.Delete(bin);
        }
    }

    [Fact]
    public async Task Upload_Success_ReportsDeviceVersion()
    {
        (FirmwareUpdater.Result result, string? contentType) =
            await RunAgainstStub(200, "{\"ok\":true,\"version\":\"2.1.0\"}");
        Assert.True(result.Ok);
        Assert.Equal("2.1.0", result.DeviceVersion);
        Assert.Contains("multipart/form-data", contentType);
    }

    [Fact]
    public async Task Upload_ServerError_ReportsFailure()
    {
        (FirmwareUpdater.Result result, _) =
            await RunAgainstStub(500, "{\"error\":\"update failed: bad magic\"}");
        Assert.False(result.Ok);
        Assert.Contains("bad magic", result.Error);
    }

    [Fact]
    public async Task Upload_UnreachableHost_ReportsFailure()
    {
        var updater = new FirmwareUpdater(TimeSpan.FromSeconds(2));
        string bin = Path.Combine(Path.GetTempPath(), $"ltfw-nx-{Guid.NewGuid():N}.bin");
        File.WriteAllBytes(bin, [1, 2, 3]);
        try
        {
            FirmwareUpdater.Result result = await updater.UploadAsync("127.0.0.1:9", bin, CancellationToken.None);
            Assert.False(result.Ok);
            Assert.NotNull(result.Error);
        }
        finally
        {
            File.Delete(bin);
        }
    }
}
