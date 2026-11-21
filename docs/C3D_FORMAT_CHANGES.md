# C3D format changes in v1.1.0

This note is for anyone who records with LibreMotion-Capture or reads its C3D files. v1.1.0 adds five analog
channels and one exported function, so that a C3D file can carry what an application needs to recompute its
results from the file alone: exact time stamps, each device's tracking state, and whether the headset had a pose.
Nothing that existed in v1.0.0 changes its label, index or meaning. An application that does not call the new
function records as before, apart from the appended channels.

## Why

In v1.0.0 a C3D file could not reproduce an application's own pose log exactly. Measured on a simulated session of
VR-ROMIK (259,316 frames), the v1.0.0 files had four losses:

- **Time.** `RealTime` is float32. After about 20 minutes of session time its steps are about 0.1 ms, so a frame at
  the edge of a time window (for example a cued hold) can fall just outside it.
- **Headset presence.** The headset point was always written as present. A headset without a pose was written as a
  tracked headset at the origin.
- **Tracking state.** Only "controller present or not" reached the file. A controller whose position or orientation
  was not tracked, but which still had a pose, read as fully tracked.
- **The last sample of a trial** was missing in VR-ROMIK's files. That was an application issue, fixed in VR-ROMIK,
  not in the library.

## What changed

### New exported function

```c
void telemetry_record_frame_ex(const VRFrameDataPlain* frame, const VRFrameValidityPlain* validity);

typedef struct {
    int      hmdHasPose;  /* 0: the headset has no pose in this frame */
    unsigned hmdFlags;    /* the application's tracking bits for the headset, stored verbatim */
    unsigned leftFlags;   /* left controller */
    unsigned rightFlags;  /* right controller */
} VRFrameValidityPlain;
```

- Call it instead of `telemetry_record_frame` to record the frame together with its tracking state.
- `validity == NULL` records exactly as `telemetry_record_frame` does.
- `VRFrameDataPlain` and every existing exported function are unchanged.
- The C# binding (`Unity_Implementation/Plugins/telemetria/TelemetriaAPI.cs`) declares the function and the struct
  (`VRFrameValidityPlain`, sequential layout).
- The JSON upload is unchanged and does not carry the validity.

### New analog channels (appended after `RealTime`)

| Index | Label | Content |
| --- | --- | --- |
| 213 | `RealTime_s` | Whole seconds of the frame's time stamp, `floor(timestampSec)` (exact). |
| 214 | `RealTime_frac` | The rest, `timestampSec - floor(timestampSec)`, in [0, 1). |
| 215 | `HMD_flags` | `validity->hmdFlags`, or -1 when the frame was recorded without validity. |
| 216 | `L_flags` | `validity->leftFlags`, or -1. |
| 217 | `R_flags` | `validity->rightFlags`, or -1. |

- The file now has 218 analog channels instead of 213. The 59 points are unchanged.
- `RealTime` (index 212) is still written, as before. It is no longer the last channel.
- `RealTime_s + RealTime_frac` gives the double time stamp to about 1e-7 s.
- The three time channels are written for every frame, whichever function recorded it.
- The flag bits are the application's own; the library does not interpret them. They are stored as exact integers
  in float32, which holds any value below 2^24.

### Headset presence

When a frame is recorded with `validity->hmdHasPose == 0`:

- `HEAD`, `LFHD` and `RFHD` get residual -1 (missing);
- `HMD_qx` ... `HMD_qw` are 0.

Without validity, the headset is written as present, as in v1.0.0.

## Compatibility

| Who | What happens with v1.1.0 |
| --- | --- |
| An application that keeps calling `telemetry_record_frame` | Records as before. Its files gain channels 213-217: the two time channels, and -1 in the three flag channels. |
| A reader that finds channels by label, or by index up to 212 | Reads files of both versions unchanged. |
| A reader that takes "the last analog channel" as `RealTime` | Must look `RealTime` up by label (index 212). This is the only change a reader can need. |
| An application binary built against v1.0.0 | Runs with the v1.1.0 library: no struct or signature it uses changed. |

## Reading the new channels

Find channels by label. A file without `RealTime_s` is a v1.0.0 file: take `RealTime` as the time and treat the
flags as not reported.

```python
# labels: the ANALOG:LABELS parameter; analog: frames x channels
idx = {name.strip(): i for i, name in enumerate(labels)}
if "RealTime_s" in idx:                     # v1.1.0 or later
    t = analog[:, idx["RealTime_s"]].astype(float) + analog[:, idx["RealTime_frac"]]
    hmd_flags = analog[:, idx["HMD_flags"]]  # -1 = not reported by the application
else:                                       # v1.0.0
    t = analog[:, idx["RealTime"]].astype(float)
    hmd_flags = None
```

To find out whether the headset had a pose, look at the residual of `HEAD`: -1 means missing.

## Verification

- **Library tests:** 43 pass on Windows (MSVC), 5 of them new:
  - `C3dValidity.NewChannelsAreAppendedAfterRealTime`
  - `C3dValidity.PlainRecordKeepsTodaysHeadAndMarksFlagsNotReported`
  - `C3dValidity.ReportedFlagsAndHeadsetWithoutPose`
  - `C3dValidity.TwoPartTimeKeepsTheDoubleTimeStamp`
  - `ApiFrameRate.RecordFrameExCarriesValidity`

  One existing assertion changed: it checked that `RealTime` is the last channel.
- **Builds:** both binaries pass the post-build export check (Windows x64: 7 entry points; Android arm64: also
  `telemetry_set_java_context` and `JNI_OnLoad`).
- **Measured in VR-ROMIK on simulated sessions:**
  - normal session: 259,316 frames, 0 unmatched against the application's pose log; time stamps equal after rounding
    to the microsecond; flag channels equal to the pose log's flags in every frame;
  - fault session: lost and partially tracked controllers carry the same presence and flags as the pose log.
- **Not yet done:** the headset-without-pose case is covered by the library tests only; no session removes the
  headset's pose. Nothing has run on a headset yet, which is why this release is marked pre-release.
