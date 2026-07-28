using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;

namespace LaserTag.Web.Tests;

/// <summary>
/// Boots the real <c>LaserTag.Web</c> app in-process for integration tests.
/// </summary>
/// <remarks>
/// <see cref="Program"/> skips <c>UseUrls("http://0.0.0.0:5080")</c> only
/// when the environment is <c>Testing</c> — without setting it here, every
/// test run would fight over the same fixed port instead of the ephemeral
/// one <see cref="WebApplicationFactory{TEntryPoint}"/> assigns.
/// </remarks>
public sealed class LaserTagWebFactory : WebApplicationFactory<Program>
{
    /// <inheritdoc/>
    protected override void ConfigureWebHost(IWebHostBuilder builder) =>
        builder.UseEnvironment("Testing");
}
