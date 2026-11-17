# Recording at the headset's refresh rate

How to make a Unity app that uses LibreMotion-Capture record at the **actual refresh rate of
the headset** (one sample per displayed frame) instead of the fixed `frameRate` in
`initialConfig.json`. It is already done in VR-CROM (Unity 2021.2, OpenXR on PC, Oculus on
Quest) and VR-AppleFarm (Unity 2018.4, built-in OpenVR/Oculus).

## Why

`TelemetriaBootstrap` samples poses in `Application.onBeforeRender`, so a sample can only be
taken when a frame is rendered. If `frameRate` does not match the display:

- 60 Hz on a 72 Hz Quest: about one frame in six is skipped, and the samples are unevenly spaced.
- 72 Hz on a 90 Hz Vive Pro or Reverb G2: each sample comes from the nearest frame, with up to
  ±5.5 ms of timing error, while the C3D file says the samples are evenly spaced at 72 Hz.
  That error shows up in velocities and accelerations.

Typical refresh rates: Meta Quest 72 Hz by default (80/90/120 Hz if the app requests it),
Quest via Link the rate set in the Meta app, Vive Pro 90 Hz, HP Reverb G2 90 Hz (60 Hz
optional), Rift S 80 Hz, Valve Index 80/90/120/144 Hz.

## The API: `telemetry_set_frame_rate`

```c
void telemetry_set_frame_rate(int hz);   // C#: TelemetriaAPI.telemetry_set_frame_rate(hz)
```

Call it **before** `telemetry_initialize`:

| `hz` | Effect |
| --- | --- |
| 1 to 240 | the next sessions use `hz` instead of `frameRate` from `initialConfig.json` |
| 0 | clears the override: `frameRate` from the config is used again (60 if there is none) |
| anything else | ignored; the previous value (override or none) is kept |

- The value **persists** across `telemetry_shutdown`/`telemetry_initialize` until it is changed,
  so an app that runs several sessions per run (VR-CROM) can set it once. Calling it before
  every `telemetry_initialize` is also fine.
- A call while a session is running only affects the next session.
- `telemetry_initialize` returns the rate it used and writes the same value into the C3D header
  (`POINT:RATE`, `ANALOG:RATE`), so the sampling period (`1.0 / rc`) and the file always agree.
- The library logs where the rate came from, e.g.
  `frameRate=90 (source: telemetry_set_frame_rate; initialConfig.json has 72)` or
  `frameRate=72 (source: initialConfig.json)`.
- `initialConfig.json` is never written.

Binaries built before this function existed do not export it (`EntryPointNotFoundException`
in C#), so update the C# bindings and the `telemetria.dll`/AAR together.

## Steps

### 1. Add `HmdRefreshRate.cs`

Copy [`Scripts/HmdRefreshRate.cs`](Scripts/HmdRefreshRate.cs) into the project (any folder
under `Assets/`). It is a static class that starts itself at launch
(`RuntimeInitializeOnLoadMethod`), so no scene changes are needed. It exposes:

| Member | Meaning |
| --- | --- |
| `HmdRefreshRate.Resolved` | `true` once the rate is known or it has given up |
| `HmdRefreshRate.Hz` | rate in Hz, or `0` if unknown |
| `HmdRefreshRate.Source` | where it came from, for the log: `XR plug-in`, `measured`, `no headset`, `Android, not reported` |

How it finds the rate **on PC**:

1. **Ask the XR plug-in**: `XRDisplaySubsystem.TryGetDisplayRefreshRate`, then
   `XRDevice.refreshRate`. Oculus and OpenVR report it. OpenXR may not on SteamVR or Windows
   Mixed Reality, because core OpenXR has no refresh-rate query (only Meta's
   `XR_FB_display_refresh_rate` extension).
2. **Otherwise measure it**: median `onBeforeRender` interval over 90 frames, after 1 s of
   margin for loading hitches, snapped to the nearest standard rate (60, 72, 80, 90, 100, 120,
   144) within 4%. If the app cannot keep up and the runtime reprojects, this gives the rate
   actually rendered (e.g. 45 Hz on a 90 Hz headset). That is the rate at which samples can
   really be taken, so it is still the right value to record.
3. **Give up after 10 s** with no headset active: `Hz = 0`, `initialConfig.json` decides.

**On Android (Meta Quest)** only the value the XR plug-in reports is accepted (30–240 Hz).
The frame interval is **never measured** there: a Quest that drops frames while the app loads
could settle on 36 Hz and record the whole session at that rate. If the plug-in reports
nothing valid within 3 s, `Hz = 0` with source `Android, not reported`, and `frameRate` from
the config is used, so keep it at the Quest rate (72 Hz unless the app requests another) in
the default config. The expected Quest value is 72 Hz.

**Unity 2018.4 / built-in XR** (no XR Plug-in Management): `XRDisplaySubsystem` and
`SubsystemManager` do not exist. Replace the `ActiveDisplay()` check and the
`TryGetDisplayRefreshRate` call with `XRSettings.enabled && XRDevice.isPresent` and
`XRDevice.refreshRate`. VR-AppleFarm's `Assets/Scripts/HmdRefreshRate.cs` is that version.

### 2. Wait for the rate before `telemetry_initialize`

If initialization happens in `Awake`/`Start`, move it to an `Initialize()` method and defer it
when the rate is not known yet:

```csharp
void Awake()
{
    if (HmdRefreshRate.Resolved) Initialize();
    else StartCoroutine(InitializeWhenRateKnown());
}

IEnumerator InitializeWhenRateKnown()
{
    while (!HmdRefreshRate.Resolved) yield return null;
    Initialize();
}
```

- If the first scene is a menu that is not recorded (VR-CROM), the rate is always known before
  recording starts.
- If recording starts in the first scene (VR-AppleFarm), it starts a few frames late
  (immediately with the plug-in value, about 2.5 s later if the rate has to be measured on PC,
  at most 3 s on Quest if the plug-in never reports it).
- If the app can stop recording explicitly, set a flag in the stop method and check it after
  the wait, so a stopped session never starts late (see `stopRequested` in VR-AppleFarm's
  `MotionCapture.cs`).
- The tick must ignore frames until initialization is done (`if (!initialized) return;`).

### 3. Pass the rate before `telemetry_initialize`

```csharp
// Read-only check: initialConfig.json is never modified
static readonly Regex HmdRateDisabled = new Regex(@"""useHmdRefreshRate""\s*:\s*false");

// Headset rate if known and not disabled in the config; 0 = use the config's frameRate
static int HmdFrameRate(string configPath)
{
    if (HmdRefreshRate.Hz <= 0) return 0;
    try
    {
        if (File.Exists(configPath) && HmdRateDisabled.IsMatch(File.ReadAllText(configPath))) return 0;
    }
    catch (Exception e) { Debug.LogWarning("[Telemetria] could not read " + configPath + ": " + e.Message); }
    return HmdRefreshRate.Hz;
}
```

And in `Initialize()`:

```csharp
TelemetriaAPI.telemetry_set_frame_rate(HmdFrameRate(configPath));
int rc = TelemetriaAPI.telemetry_initialize(ref cfg);
frameRateHz = (rc >= 1 && rc <= 240) ? rc : 60;
periodSec = 1.0 / frameRateHz;
```

`configPath` is `<baseDir>/initialConfig.json` on PC and
`Application.persistentDataPath/initialConfig.json` on Android (the library ignores `baseDir`
there and reads `/sdcard/Android/data/<package>/files`, the same folder). Passing `0` when the
rate is unknown matters for apps with several sessions: it clears an override left by an
earlier session.

### 4. Allow half a period of jitter in the tick

When the recording rate equals the display rate, a frame that arrives a fraction of a
millisecond before `nextTickSec` would be skipped. Change the check in `OnBeforeRenderTick`:

```csharp
// before
if (now < nextTickSec) return;
// after
if (now < nextTickSec - periodSec * 0.5) return;
```

`nextTickSec` still advances by exactly one period per sample, so the average rate cannot go
above `frameRate`.

### 5. Config switch and documentation

- Add `"useHmdRefreshRate": true` to the default config in `Resources` so the option is
  visible. A missing key means `true`; `false` makes the app use `frameRate` on every
  platform. The native library ignores keys it does not know.
- Say in the app's README that `frameRate` is replaced by the headset rate (on Quest only when
  the plug-in reports it) and is otherwise the fallback.

The reference [`Scripts/TelemetriaBootstrap.cs`](Scripts/TelemetriaBootstrap.cs) has all of
the above.

## Checking it

PC, in `Player.log`; Quest, with `adb logcat -s Unity telemetria`:

```
[Telemetria] headset refresh rate: 90 Hz (XR plug-in)     <- or "(measured)" on PC
[Telemetria] telemetry_initialize rc=90 ...
```

The library's own line, `frameRate=90 (source: telemetry_set_frame_rate; initialConfig.json
has 72)`, is in logcat (tag `telemetria`) on Quest; on Windows it goes to `OutputDebugString`
(visible with DebugView or an attached debugger), not to `Player.log`.

`rc` is the rate written in the C3D. In the C3D, `POINT:RATE` must be 90 and the number of
frames about 90 × the session length in seconds. `initialConfig.json` must be unchanged.

Things to test on each headset:

- the rate reported matches the headset setting;
- on Quest the source is `XR plug-in` (72 Hz), or `Android, not reported` with `rc=72`, never
  a measurement;
- with a menu-less first scene, recording starts within a few seconds;
- with `"useHmdRefreshRate": false`, `rc` equals `frameRate`;
- PC, starting with the headset off: after 10 s the log says `unknown (no headset)` and
  `frameRate` is used.

## Limitations

- The rate is found once per run. If the user changes the headset's refresh rate while the
  app is running, the next sessions keep the old value until the app restarts.
- On PC, if the app is reprojecting during the 90 measured frames, the measured value is the
  reduced rate even if it later recovers. This only happens when the plug-in does not report
  the rate.
- On Quest, an app that requests another display rate after launch (e.g. 90 Hz through the
  Oculus/OpenXR settings at runtime) is recorded at the rate reported at launch.
