using System;
using System.Collections;
using System.IO;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using UnityEngine;
using UnityEngine.XR;
using UnityEngine.XR.Hands;
using UnityEngine.XR.Management;

// Graba a la frecuencia real del visor (HmdRefreshRate.cs, ver HMD_REFRESH_RATE.md): espera
// a conocerla, la pasa con telemetry_set_frame_rate y despues llama a telemetry_initialize.
// "useHmdRefreshRate": false en initialConfig.json lo desactiva (se usa su frameRate).
public class TelemetriaBootstrap : MonoBehaviour
{
    // Son los valores de configuracion para la bbdd
    IntPtr sessPtr;
    // Para obtener los dispositivos XR y sus estados
    IntPtr devInfoPtr;
    // Directorio base para initialConfig.json y salida .c3d (Application.dataPath)
    IntPtr baseDirPtr;
    List<InputDevice> devs = new List<InputDevice>();
    InputDevice hmd, left, right, leftMano, rightMano;
    // XR Hands subsystem
    XRHandSubsystem _xrHands;

    // === Rate limiting ===
        // Frecuencia efectiva devuelta por telemetry_initialize (en Hz, 1..240)
        int frameRateHz = 60;
        // Periodo entre envios (segundos) basado en frameRateHz
        double periodSec;
        // Proximo instante absoluto (realtime) en el que se permite enviar (tope por segundo)
        double nextTickSec = 0.0;
        double t0 = 0.0;
        bool zeroed = false;

    // Suscripcion a onBeforeRender
    bool subscribed = false;
    // telemetry_initialize ya se llamo (la inicializacion espera a HmdRefreshRate)
    bool initialized = false;

    // Solo lectura: initialConfig.json nunca se modifica
    static readonly Regex HmdRateDisabled = new Regex(@"""useHmdRefreshRate""\s*:\s*false");

    //flags de botones
    bool fHand, fPrimary, fSecondary, fGrip, fTrigger, fJoystick;
    // Joints a capturar
    Unity.Collections.NativeArray<bool> jointsAvailable;

    // ADD: helper. Aplica tu misma conversion de ejes que para HMD/mandos.
    bool TryWriteJoint(XRHand hand, XRHandJointID id, out JointSamplePlain js)
    {
        js = default;
        XRHandJoint joint = hand.GetJoint(id);

        // trackingState raw
        int state1 = (int)joint.trackingState;
        bool flagPose = (joint.trackingState & XRHandJointTrackingState.Pose) != 0;

        // pose
        Pose pose;
        bool gotPos = joint.TryGetPose(out pose);
        Vector3 p = gotPos ? pose.position : Vector3.zero;
        Quaternion q = gotPos ? pose.rotation : Quaternion.identity;

        // aplica tu conversion de ejes (invertir Z y ajustar quat)
        p.z = -p.z;
        q = new Quaternion(-q.x, -q.y, q.z, q.w);

        js = new JointSamplePlain
        {
            idIndex = XRHandJointIDUtility.ToIndex(id),
            state = state1,
            hasPose = (byte)((flagPose && gotPos) ? 1 : 0),
            px = p.x, py = p.y, pz = p.z,
            qx = q.x, qy = q.y, qz = q.z, qw = q.w,
        };
        return true;
    }

    // Genera un sessionId simple pero legible app + fecha + sufijo aleatorio// Ejemplo: MiApp-20251002-104512-a3f91b
    static string GenerateSessionIdHuman(string appPrefix)
    {
        string ts = System.DateTime.UtcNow.ToString("yyyyMMdd-HHmmss");
        string rnd = System.Guid.NewGuid().ToString("N").Substring(0, 6); // 6 hex
        return $"{appPrefix}-{ts}-{rnd}";
    }

    // Carpeta donde el nativo busca initialConfig.json: baseDir en PC, la carpeta de la app
    // en Android (/storage/emulated/0/Android/data/<paquete>/files = persistentDataPath)
    static string ConfigPath()
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        return Path.Combine(Application.persistentDataPath, "initialConfig.json");
#else
        return Path.Combine(Application.dataPath, "initialConfig.json");
#endif
    }

    // Frecuencia del visor si se conoce y el config no la desactiva; 0 = usar frameRate del config
    static int HmdFrameRate()
    {
        if (HmdRefreshRate.Hz <= 0) return 0;
        try
        {
            string path = ConfigPath();
            if (File.Exists(path) && HmdRateDisabled.IsMatch(File.ReadAllText(path))) return 0;
        }
        catch (Exception e) { Debug.LogWarning("[Telemetria] no se pudo leer " + ConfigPath() + ": " + e.Message); }
        return HmdRefreshRate.Hz;
    }

    void Awake()
    {
        // La frecuencia del visor se conoce en los primeros frames de la app; si aun no
        // esta, se espera y la grabacion empieza unos frames mas tarde.
        if (HmdRefreshRate.Resolved) Initialize();
        else StartCoroutine(InitializeWhenRateKnown());
    }

    IEnumerator InitializeWhenRateKnown()
    {
        while (!HmdRefreshRate.Resolved) yield return null;
        Initialize();
    }

    void Initialize()
    {
        //hand tracking 
        var loader = XRGeneralSettings.Instance?.Manager?.activeLoader;
        if (loader != null)
            _xrHands = loader.GetLoadedSubsystem<XRHandSubsystem>();

#if UNITY_ANDROID
        TelemetriaAPI.InyectarContextoJava();
#endif

        // Construye los strings que iran al nativo
        string sessionId = GenerateSessionIdHuman(Application.productName);
        string deviceInfo = SystemInfo.deviceModel + " - " + Application.productName;     //crea un string con modelo de gafas y nombre de tu app

        // Reserva memoria nativa y copia como char*
        sessPtr = Marshal.StringToHGlobalAnsi(sessionId);
        devInfoPtr = Marshal.StringToHGlobalAnsi(deviceInfo);
        // baseDir: donde el nativo busca initialConfig.json y escribe los .c3d (Windows/Standalone)
        baseDirPtr = Marshal.StringToHGlobalAnsi(Application.dataPath);

        var cfg = new TelemetryConfigPlain
        {
            sessionId = sessPtr,
            deviceInfo = devInfoPtr,
            baseDir = baseDirPtr
        };

        // Antes de initialize: frecuencia del visor, o 0 para usar frameRate del config
        int hmdHz = HmdFrameRate();
        TelemetriaAPI.telemetry_set_frame_rate(hmdHz);
        int rc = TelemetriaAPI.telemetry_initialize(ref cfg);
        initialized = true;
        Debug.Log("telemetry_initialize rc=" + rc + " (visor: " + (hmdHz > 0 ? hmdHz + " Hz" : "no usado")
                  + ", " + HmdRefreshRate.Source + ")");

        // Validar y fijar la frecuencia efectiva (Hz)
        if (rc >= 1 && rc <= 240)
            frameRateHz = rc;
        else
            frameRateHz = 60;
        periodSec = 1.0 / Math.Max(1, frameRateHz);

        // mira que botones enviamos. flags es el bitmask y miramos bit a bit y lo convertimos a bools
        uint flags = TelemetriaAPI.telemetry_get_feature_flags();
        fHand = (flags & (1u << 0)) != 0;
        fPrimary = (flags & (1u << 1)) != 0;
        fSecondary = (flags & (1u << 2)) != 0;
        fGrip = (flags & (1u << 3)) != 0;
        fTrigger = (flags & (1u << 4)) != 0;
        fJoystick = (flags & (1u << 5)) != 0;
        Debug.Log($"[TelemetryFlags] raw=0x{flags:X8}");

        // Asignar dispositivos XR
        AsignarDispositivos();

        if (fHand)
        {
            if (_xrHands == null) { Debug.Log("Telemetry configured to send hand joints, but XRHandSubsystem is not available."); }
            else { jointsAvailable = _xrHands.jointsInLayout; }
        }
    }

    void OnEnable()
    {
        InputDevices.deviceConnected += OnDeviceEvent;
        InputDevices.deviceDisconnected += OnDeviceEvent;

        // Suscribimos el tick justo antes de renderizar (pose mas reciente posible)
        Application.onBeforeRender += OnBeforeRenderTick;
        subscribed = true;
    }

    void OnDisable()
    {
        InputDevices.deviceConnected -= OnDeviceEvent;
        InputDevices.deviceDisconnected -= OnDeviceEvent;

        if (subscribed)
        {
            Application.onBeforeRender -= OnBeforeRenderTick;
            subscribed = false;
        }
    }

    // Tick justo antes de renderizar: aplicamos rate limiting (por segundo) // cambiamos el sentido de eje Z para standard OpenXR
    void OnBeforeRenderTick()
    {
        if (!initialized) return;
        double now = Time.realtimeSinceStartupAsDouble;
        //para que el primer frame tenga timestamp 0
        if (!zeroed)
        {   
            t0 = now;           
            zeroed = true;
        }

        // --- Tope por segundo (periodo basado en frameRateHz) ---
        // Medio periodo de margen: a la frecuencia del visor, un frame que llega una
        // fraccion de ms antes de tiempo no debe descartarse
        if (now < nextTickSec - periodSec * 0.5)
            return;

        // Avanzar el tick. Si hubo mucha demora, actualizamos el tiempo del siguiente frame para evitar rafagas.
        const double MaxLateness = 0.5; // segundos
        if (now - nextTickSec > MaxLateness)
            nextTickSec = now + periodSec;
        else
            nextTickSec += periodSec;

        // ===== Captura y envio SOLO cuando toca =====
        var frame = new VRFrameDataPlain();
        frame.timestampSec = Math.Max(0.0, now - t0);  // 0 en el primer envío

        // HMD
        if (hmd.isValid &&
            hmd.TryGetFeatureValue(CommonUsages.centerEyePosition, out Vector3 hp) &&
            hmd.TryGetFeatureValue(CommonUsages.centerEyeRotation, out Quaternion hr))
        {
            frame.hmdPose.position = new float[] { hp.x, hp.y, -hp.z };
            frame.hmdPose.rotation = new float[] { -hr.x, -hr.y, hr.z, hr.w };
        }
        else
        {
            frame.hmdPose.position = new float[] { 0, 0, 0 };
            frame.hmdPose.rotation = new float[] { 0, 0, 0, 1 };
        }

        // Left controller
        frame.leftCtrl.isActive = left.isValid ? 1 : 0;
        if (left.isValid)
        {
            left.TryGetFeatureValue(CommonUsages.devicePosition, out Vector3 lp);
            left.TryGetFeatureValue(CommonUsages.deviceRotation, out Quaternion lr);
            frame.leftCtrl.pose.position = new float[] { lp.x, lp.y, -lp.z };
            frame.leftCtrl.pose.rotation = new float[] { -lr.x, -lr.y, lr.z, lr.w };

            if (fTrigger && left.TryGetFeatureValue(CommonUsages.trigger, out float ltrig))
                frame.leftCtrl.trigger = ltrig;
            else
                frame.leftCtrl.trigger = 0f;

            if (fGrip && left.TryGetFeatureValue(CommonUsages.grip, out float lgrip))
                frame.leftCtrl.grip = lgrip;
            else
                frame.leftCtrl.grip = 0f;

            if (fPrimary && left.TryGetFeatureValue(CommonUsages.primaryButton, out bool lPrim) && lPrim)
                frame.leftCtrl.buttons |= 0x1u;          // PRIMARY bit
            if (fSecondary && left.TryGetFeatureValue(CommonUsages.secondaryButton, out bool lSec) && lSec)
                frame.leftCtrl.buttons |= 0x2u;          // SECONDARY bit
            if (fJoystick)
            {
                // Thumbstick click suele venir como primary2DAxisClick
                if (left.TryGetFeatureValue(CommonUsages.primary2DAxis, out Vector2 laxis))
                {
                    frame.leftCtrl.stickX = laxis.x;
                    frame.leftCtrl.stickY = laxis.y;
                }
            }
        }
        else
        {
            frame.leftCtrl.pose.position = new float[] { 0, 0, 0 };
            frame.leftCtrl.pose.rotation = new float[] { 0, 0, 0, 1 };
            frame.leftCtrl.trigger = 0f; frame.leftCtrl.grip = 0f; frame.leftCtrl.buttons = 0u;

        }

        // Right controller
        frame.rightCtrl.isActive = right.isValid ? 1 : 0;
        if (right.isValid)
        {
            right.TryGetFeatureValue(CommonUsages.devicePosition, out Vector3 rp);
            right.TryGetFeatureValue(CommonUsages.deviceRotation, out Quaternion rrq);
            frame.rightCtrl.pose.position = new float[] { rp.x, rp.y, -rp.z };
            frame.rightCtrl.pose.rotation = new float[] { -rrq.x, -rrq.y, rrq.z, rrq.w };

            if (fTrigger && right.TryGetFeatureValue(CommonUsages.trigger, out float rtrig))
                frame.rightCtrl.trigger = rtrig;
            else
                frame.rightCtrl.trigger = 0f;

            if (fGrip && right.TryGetFeatureValue(CommonUsages.grip, out float rgrip))
                frame.rightCtrl.grip = rgrip;
            else
                frame.rightCtrl.grip = 0f;

            if (fPrimary && right.TryGetFeatureValue(CommonUsages.primaryButton, out bool rPrim) && rPrim)
                frame.rightCtrl.buttons |= 0x1u;
            if (fSecondary && right.TryGetFeatureValue(CommonUsages.secondaryButton, out bool rSec) && rSec)
                frame.rightCtrl.buttons |= 0x2u;
            if (fJoystick)
            {
                if (right.TryGetFeatureValue(CommonUsages.primary2DAxis, out Vector2 raxis)) { 
                    frame.rightCtrl.stickX = raxis.x;
                    frame.rightCtrl.stickY = raxis.y;
                }
            }
        }
        else
        {
            frame.rightCtrl.pose.position = new float[] { 0, 0, 0 };
            frame.rightCtrl.pose.rotation = new float[] { 0, 0, 0, 1 };
            frame.rightCtrl.trigger = 0f; frame.rightCtrl.grip = 0f; frame.rightCtrl.buttons = 0u;
        }

        // Hands joints 
        if ( fHand && _xrHands != null)
        {
            // LEFT
            var left = _xrHands.leftHand;
            if (left.isTracked && jointsAvailable != null)
            {
                var leftList = new List<JointSamplePlain>(jointsAvailable.Length);
                for (int i = 0; i < jointsAvailable.Length; ++i)
                {
                    if (!jointsAvailable[i]) continue; // joint no soportado por el provider
                    var id = XRHandJointIDUtility.FromIndex(i);
                    if (TryWriteJoint(left, id, out var sample))
                        leftList.Add(sample);
                }
                frame.leftHandJoints = frame.leftHandJoints ?? new JointSamplePlain[30];
                Array.Clear(frame.leftHandJoints, 0, frame.leftHandJoints.Length);
                int lcount = Math.Min(leftList.Count, 30);
                    if (lcount > 0) leftList.CopyTo(0, frame.leftHandJoints, 0, lcount);
                frame.leftHandJointCount = lcount;
            }

            // RIGHT
            var right = _xrHands.rightHand;
            if (right.isTracked && jointsAvailable != null)
            {
                var rightList = new List<JointSamplePlain>(jointsAvailable.Length);
                for (int i = 0; i < jointsAvailable.Length; ++i)
                {
                    if (!jointsAvailable[i]) continue;
                    var id = XRHandJointIDUtility.FromIndex(i);
                    if (TryWriteJoint(right, id, out var sample))
                        rightList.Add(sample);
                }
                frame.rightHandJoints = frame.rightHandJoints ?? new JointSamplePlain[30];
                Array.Clear(frame.rightHandJoints, 0, frame.rightHandJoints.Length);
                int rcount = Math.Min(rightList.Count, 30);
                    if (rcount > 0) rightList.CopyTo(0, frame.rightHandJoints, 0, rcount);
                frame.rightHandJointCount = rcount;
            }
        }
        // Enviar a la libreria
        TelemetriaAPI.telemetry_record_frame(ref frame);
    }

    void Update(){}

    void OnDestroy()
    {
        // Por si OnDisable no se llamó
        if (subscribed)
        {
            Application.onBeforeRender -= OnBeforeRenderTick;
            subscribed = false;
        }
        if (initialized)
        {
            TelemetriaAPI.telemetry_force_upload();
            TelemetriaAPI.telemetry_shutdown();
        }

        if (sessPtr != IntPtr.Zero) Marshal.FreeHGlobal(sessPtr);
        if (devInfoPtr != IntPtr.Zero) Marshal.FreeHGlobal(devInfoPtr);
        if (baseDirPtr != IntPtr.Zero) Marshal.FreeHGlobal(baseDirPtr);
    }

    void OnDeviceEvent(InputDevice device)
    {
        // Reasignar dispositivos XR al conectarse/desconectarse
        AsignarDispositivos();
    }

    void AsignarDispositivos()
    {
        devs.Clear();
        InputDevices.GetDevicesWithCharacteristics(InputDeviceCharacteristics.HeadMounted, devs);
        hmd = (devs.Count > 0) ? devs[0] : default;
        devs.Clear();

        InputDevices.GetDevicesWithCharacteristics(InputDeviceCharacteristics.Left | InputDeviceCharacteristics.Controller, devs);
        left = (devs.Count > 0) ? devs[0] : default;
        devs.Clear();

        InputDevices.GetDevicesWithCharacteristics(InputDeviceCharacteristics.Right | InputDeviceCharacteristics.Controller, devs);
        right = (devs.Count > 0) ? devs[0] : default;
        devs.Clear();

        if (fHand)
        {
            InputDevices.GetDevicesWithCharacteristics(InputDeviceCharacteristics.Left | InputDeviceCharacteristics.HandTracking, devs);
            leftMano = (devs.Count > 0) ? devs[0] : default;
            devs.Clear();

            InputDevices.GetDevicesWithCharacteristics(InputDeviceCharacteristics.Right | InputDeviceCharacteristics.HandTracking, devs);
            rightMano = (devs.Count > 0) ? devs[0] : default;
            devs.Clear();
        }
    }
}
