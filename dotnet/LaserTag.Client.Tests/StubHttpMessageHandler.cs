using System.Net;

namespace LaserTag.Client.Tests;

/// <summary>
/// A test double <see cref="HttpMessageHandler"/> that captures the outgoing
/// request and returns a canned response, so REST client tests can assert the
/// method, path and body the client issued and feed it golden JSON in return.
/// </summary>
public sealed class StubHttpMessageHandler : HttpMessageHandler
{
    private readonly Func<HttpRequestMessage, string, HttpResponseMessage> _responder;

    /// <summary>
    /// Initializes a new instance of the <see cref="StubHttpMessageHandler"/> class.
    /// </summary>
    /// <param name="responder">
    /// A callback receiving the request and its (already-read) body string, and
    /// returning the response to reply with.
    /// </param>
    public StubHttpMessageHandler(Func<HttpRequestMessage, string, HttpResponseMessage> responder)
    {
        _responder = responder;
    }

    /// <summary>Gets the most recent request the client sent through this handler.</summary>
    public HttpRequestMessage? LastRequest { get; private set; }

    /// <summary>Gets the most recent request body the client sent (empty string if none).</summary>
    public string LastRequestBody { get; private set; } = string.Empty;

    /// <inheritdoc />
    protected override async Task<HttpResponseMessage> SendAsync(
        HttpRequestMessage request,
        CancellationToken cancellationToken)
    {
        LastRequest = request;
        LastRequestBody = request.Content is null
            ? string.Empty
            : await request.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);

        return _responder(request, LastRequestBody);
    }

    /// <summary>Builds a JSON response with the given status code and body.</summary>
    /// <param name="statusCode">The status code to return.</param>
    /// <param name="json">The JSON body.</param>
    /// <returns>The constructed response message.</returns>
    public static HttpResponseMessage Json(HttpStatusCode statusCode, string json) =>
        new(statusCode)
        {
            Content = new StringContent(json, System.Text.Encoding.UTF8, "application/json"),
        };
}
