namespace LaserTag.Web.Tests;

/// <summary>
/// Guards the exact bug just fixed: the shared screens live in the
/// <c>LaserTag.Ui</c> razor class library, not <c>LaserTag.Web</c> itself.
/// They 404 unless BOTH <c>Routes.razor</c>'s <c>Router.AdditionalAssemblies</c>
/// AND <c>Program.cs</c>'s <c>MapRazorComponents&lt;App&gt;().AddAdditionalAssemblies(...)</c>
/// list that assembly — the first makes the client-side router find the
/// page, the second makes ASP.NET Core's endpoint routing serve it at all.
/// Dropping either one silently breaks every route below.
/// </summary>
public class UiRouteTests : IClassFixture<LaserTagWebFactory>
{
    private readonly HttpClient _client;

    public UiRouteTests(LaserTagWebFactory factory) => _client = factory.CreateClient();

    [Theory]
    [InlineData("/", "Devices")]
    [InlineData("/devices", "Devices")]
    [InlineData("/match", "Match")]
    [InlineData("/live", "Live")]
    [InlineData("/firmware", "Firmware")]
    public async Task Route_ReturnsOk_WithExpectedHeading(string path, string expectedHeading)
    {
        HttpResponseMessage response = await _client.GetAsync(path);
        string html = await response.Content.ReadAsStringAsync();

        response.EnsureSuccessStatusCode();
        Assert.Contains($"<h1>{expectedHeading}</h1>", html, StringComparison.Ordinal);
    }
}
