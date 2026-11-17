using System.Collections.Generic;
using System.Diagnostics;
using UnityEngine;
using UnityEngine.XR;
using Debug = UnityEngine.Debug;

// Actual refresh rate of the headset, so that LibreMotion records one sample per
// displayed frame (Vive Pro and Reverb G2: 90 Hz, Rift S: 80 Hz, Quest: 72 Hz, ...).
// For Unity 2019.3+ with XR Plug-in Management (OpenXR, Oculus, OpenVR plug-ins).
// The caller passes Hz to TelemetriaAPI.telemetry_set_frame_rate before
// telemetry_initialize. See HMD_REFRESH_RATE.md.
//
// Resolved once per run, from app launch.
// PC:
//  1. Ask the XR plug-in (XRDisplaySubsystem.TryGetDisplayRefreshRate, then
//     XRDevice.refreshRate). OpenXR on SteamVR or WMR may not report it.
//  2. Otherwise measure it: median frame interval over SampleFrames frames, after
//     WarmupSec of margin for loading, rounded to the nearest standard rate.
//  3. If no headset is active after TimeoutSec, Hz stays 0 and initialConfig.json's
//     frameRate is used.
// Android (Meta Quest):
//  Only the value the XR plug-in reports is accepted, never a measurement: a Quest that
//  drops frames while loading could settle on 36 Hz and record the whole session at that
//  rate. If the plug-in reports nothing valid within AndroidTimeoutSec, Hz stays 0
//  (source "Android, not reported") and initialConfig.json's frameRate is used.
public static class HmdRefreshRate
{
    const double WarmupSec = 1.0;
    const int SampleFrames = 90;
    const double TimeoutSec = 10.0;
    const double AndroidTimeoutSec = 3.0; // the plug-in answers within the first frames
    const float MinHz = 30f, MaxHz = 240f; // the native library accepts [1, 240]
    const float SnapTolerance = 0.04f;
    static readonly int[] StandardRates = { 60, 72, 80, 90, 100, 120, 144 };

    // Rate in Hz, or 0 if it could not be found
    public static int Hz { get; private set; }
    public static bool Resolved { get; private set; }
    // "XR plug-in", "measured", "no headset" or "Android, not reported"
    public static string Source { get; private set; }

    static readonly Stopwatch clock = new Stopwatch();
    static readonly List<double> intervals = new List<double>();
    static readonly List<XRDisplaySubsystem> displays = new List<XRDisplaySubsystem>();
    // Only read on PC (Android never measures): no CS0414 in the Android build
#pragma warning disable 0414
    static double lastFrameSec, firstXrFrameSec = -1;
#pragma warning restore 0414
    static int lastFrameCount = -1;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    static void Begin()
    {
        Resolved = false;
        Hz = 0;
        intervals.Clear();
        firstXrFrameSec = -1;
        lastFrameCount = -1;
        clock.Reset();
        clock.Start();
        Application.onBeforeRender -= OnBeforeRender;
        Application.onBeforeRender += OnBeforeRender;
    }

    static void OnBeforeRender()
    {
        // Once per frame
        if (Time.frameCount == lastFrameCount) return;
        lastFrameCount = Time.frameCount;
        double now = clock.Elapsed.TotalSeconds;

        XRDisplaySubsystem display = ActiveDisplay();
#if UNITY_ANDROID && !UNITY_EDITOR
        // Quest: the plug-in value or nothing (see the header)
        float androidHz = 0f;
        if (display == null || !display.TryGetDisplayRefreshRate(out androidHz) || !IsValid(androidHz))
            androidHz = XRDevice.refreshRate;
        if (IsValid(androidHz))
        {
            Finish(Snap(androidHz), "XR plug-in");
        }
        else if (now > AndroidTimeoutSec)
        {
            Finish(0, "Android, not reported");
        }
        return;
#else
        if (display == null)
        {
            if (now > TimeoutSec) Finish(0, "no headset");
            return;
        }

        float reported;
        if (display.TryGetDisplayRefreshRate(out reported) && IsValid(reported))
        {
            Finish(Snap(reported), "XR plug-in");
            return;
        }
        reported = XRDevice.refreshRate;
        if (IsValid(reported))
        {
            Finish(Snap(reported), "XR plug-in");
            return;
        }

        // The plug-in does not report it: measure the frame interval
        if (firstXrFrameSec < 0) firstXrFrameSec = now;
        if (now - firstXrFrameSec >= WarmupSec) intervals.Add(now - lastFrameSec);
        lastFrameSec = now;
        if (intervals.Count >= SampleFrames)
        {
            intervals.Sort();
            double median = intervals[intervals.Count / 2];
            float measured = median > 0 ? (float)(1.0 / median) : 0f;
            Finish(IsValid(measured) ? Snap(measured) : 0, "measured");
        }
#endif
    }

    static XRDisplaySubsystem ActiveDisplay()
    {
        displays.Clear();
        SubsystemManager.GetInstances(displays);
        foreach (XRDisplaySubsystem d in displays)
        {
            if (d.running) return d;
        }
        return null;
    }

    static bool IsValid(float hz)
    {
        return hz >= MinHz && hz <= MaxHz;
    }

    // 89.9 -> 90, 72.02 -> 72; anything not close to a standard rate is rounded
    static int Snap(float hz)
    {
        foreach (int rate in StandardRates)
        {
            if (Mathf.Abs(hz - rate) <= rate * SnapTolerance) return rate;
        }
        return Mathf.RoundToInt(hz);
    }

    static void Finish(int hz, string source)
    {
        Application.onBeforeRender -= OnBeforeRender;
        clock.Stop();
        intervals.Clear();
        Hz = hz;
        Source = source;
        Resolved = true;
        Debug.Log("[Telemetria] headset refresh rate: " + (hz > 0 ? hz + " Hz" : "unknown") + " (" + source + ")");
    }
}
