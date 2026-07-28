using Android.App;
using Android.Content.PM;
using Android.OS;
using Android.Views;

namespace LaserTag.App;

/// <summary>The single Android activity hosting the Blazor web view.</summary>
[Activity(
    Theme = "@style/Maui.SplashTheme",
    MainLauncher = true,
    ConfigurationChanges = ConfigChanges.ScreenSize | ConfigChanges.Orientation | ConfigChanges.UiMode |
        ConfigChanges.ScreenLayout | ConfigChanges.SmallestScreenSize | ConfigChanges.Density)]
public class MainActivity : MauiAppCompatActivity
{
    /// <inheritdoc/>
    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);

        // Keep the screen awake while the app is foreground. A backgrounded or
        // dozing phone stops servicing the UDP socket promptly, which would
        // drop hits mid-match; a keep-screen-on flag is far simpler than a
        // foreground service and honest about the trade (the phone is the game
        // host, so it is expected to stay awake and plugged in if long).
        Window?.AddFlags(WindowManagerFlags.KeepScreenOn);
    }
}
