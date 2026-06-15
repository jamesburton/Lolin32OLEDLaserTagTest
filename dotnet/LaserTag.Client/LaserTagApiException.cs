using System.Net;

namespace LaserTag.Client;

/// <summary>
/// Thrown when a device REST call returns a non-success (non-2xx) status code
/// (design §8). Carries the HTTP status code and, when present, the device's
/// <c>{"error": "..."}</c> message body.
/// </summary>
public sealed class LaserTagApiException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="LaserTagApiException"/> class.
    /// </summary>
    /// <param name="statusCode">The HTTP status code returned by the device.</param>
    /// <param name="errorMessage">
    /// The device-supplied error message parsed from the <c>error</c> field of a
    /// JSON error body, or <see langword="null"/> if none was present.
    /// </param>
    /// <param name="rawBody">The raw response body, for diagnostics.</param>
    public LaserTagApiException(HttpStatusCode statusCode, string? errorMessage, string? rawBody)
        : base(BuildMessage(statusCode, errorMessage))
    {
        StatusCode = statusCode;
        ErrorMessage = errorMessage;
        RawBody = rawBody;
    }

    /// <summary>Gets the HTTP status code returned by the device.</summary>
    public HttpStatusCode StatusCode { get; }

    /// <summary>
    /// Gets the device-supplied error message (the <c>error</c> JSON field), or
    /// <see langword="null"/> if the body carried none.
    /// </summary>
    public string? ErrorMessage { get; }

    /// <summary>Gets the raw response body for diagnostics, if any.</summary>
    public string? RawBody { get; }

    private static string BuildMessage(HttpStatusCode statusCode, string? errorMessage)
    {
        int code = (int)statusCode;
        return errorMessage is { Length: > 0 }
            ? $"Device returned HTTP {code} ({statusCode}): {errorMessage}"
            : $"Device returned HTTP {code} ({statusCode}).";
    }
}
