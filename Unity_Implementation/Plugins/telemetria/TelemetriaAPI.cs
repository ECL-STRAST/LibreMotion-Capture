using System;
using System.Runtime.InteropServices;
using UnityEngine;

public static class TelemetriaAPI
{
#if UNITY_ANDROID
    [DllImport("telemetria")]
    private static extern void telemetry_set_java_context(IntPtr vm, IntPtr activity);
#endif

    // Fija la frecuencia de muestreo de la proxima telemetry_initialize, por encima del
    // frameRate de initialConfig.json. hz en [1, 240] la fija, 0 la anula (vuelve el
    // frameRate del config) y cualquier otro valor se ignora. Llamar antes de initialize;
    // el valor se mantiene entre sesiones hasta que se cambie.
    [DllImport("telemetria")]
    public static extern void telemetry_set_frame_rate(int hz);

    [DllImport("telemetria")]
    public static extern int telemetry_initialize(ref TelemetryConfigPlain cfg);

    [DllImport("telemetria")]
    public static extern void telemetry_record_frame(ref VRFrameDataPlain frame);

    // Same, plus the frame's tracking state for the C3D (headset pose present, per-device flags).
    // v1.1.0 and later; older binaries do not export it. See docs/C3D_FORMAT_CHANGES.md.
    [DllImport("telemetria")]
    public static extern void telemetry_record_frame_ex(ref VRFrameDataPlain frame, ref VRFrameValidityPlain validity);

    [DllImport("telemetria")]
    public static extern void telemetry_force_upload();

    [DllImport("telemetria")]
    public static extern void telemetry_shutdown();
    // Devuelve un bitmask con los botones para enviar. LLamar despues de initialize 
    [DllImport("telemetria")]
    public static extern uint telemetry_get_feature_flags();

#if UNITY_ANDROID
    public static void InyectarContextoJava()
    {
#if !UNITY_EDITOR
        using (var unityPlayer = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
        using (var activity = unityPlayer.GetStatic<AndroidJavaObject>("currentActivity"))
        {
            telemetry_set_java_context(IntPtr.Zero, activity.GetRawObject());
        }
#endif
    }
#endif
}

[StructLayout(LayoutKind.Sequential)]
public struct TelemetryConfigPlain
{
    public IntPtr sessionId;
    public IntPtr deviceInfo;
    // Debe ser el ultimo campo: coincide con el orden del struct C
    // TelemetryConfigPlain {sessionId, deviceInfo, baseDir} (cpp/core/TelemetryTypes.h)
    public IntPtr baseDir;
}

[StructLayout(LayoutKind.Sequential)]
public struct VRPosePlain
{
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
    public float[] position;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
    public float[] rotation;
}

[StructLayout(LayoutKind.Sequential)]
public struct ControllerStatePlain
{
    public VRPosePlain pose;
    public uint buttons;
    public float trigger;
    public float grip;
    // Analog stick
    public float stickX;
    public float stickY;
    public int isActive;
}

public struct JointSamplePlain
{
    public int idIndex;    // indice XRHandJointID (XRHandJointIDUtility.ToIndex)
    public int state;      // trackingState raw (int)
    public float px, py, pz;
    public float qx, qy, qz, qw;
    public byte hasPose;
}


[StructLayout(LayoutKind.Sequential)]
public struct VRFrameDataPlain
{
    public double timestampSec;
    public VRPosePlain hmdPose;
    public ControllerStatePlain leftCtrl;
    public ControllerStatePlain rightCtrl;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 30)] // el marshall sirve para convertir a un array fijo
    public JointSamplePlain[] leftHandJoints;
    public int leftHandJointCount;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 30)]
    public JointSamplePlain[] rightHandJoints;
    public int rightHandJointCount;
}

// Matches struct VRFrameValidityPlain (cpp/core/VRTypes.h).
[StructLayout(LayoutKind.Sequential)]
public struct VRFrameValidityPlain
{
    public int hmdHasPose;   // 0 = the headset has no pose: HEAD/LFHD/RFHD missing, HMD_q* 0
    public uint hmdFlags;    // stored verbatim in the C3D channels HMD_flags, L_flags, R_flags
    public uint leftFlags;
    public uint rightFlags;
}
