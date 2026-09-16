# Telemetria – Unity Standalone / Windows x64

Guía de integración de la librería **Telemetria** en **Unity (Standalone/Windows, x64)**
para PC-VR con runtime **SteamVR** (OpenXR). Complementa a `README_Unity.md` (Android/Quest).

La capa C# de captura es la misma que en Android (`TelemetriaBootstrap.cs`); la única
diferencia en el config plano es el nuevo campo `baseDir` (3er campo del struct C,
ver `cpp/core/TelemetryTypes.h`), que el bootstrap fija a `Application.dataPath`.

---

## 1. Compilar la librería nativa

Desde `cpp/` (ver `cpp/README-build.md`), genera `telemetria.dll` para Win64 con
CMake + MSVC o MinGW-w64:

```
cmake --build build-win --config Release   # resultado: telemetria.dll
```

## 2. Colocar el plugin en el proyecto Unity

```
Assets/
├── Plugins/
│   └── x86_64/
│       └── telemetria.dll
├── Plugins/telemetria/
│   └── TelemetriaAPI.cs        # mismo archivo que Android (compartido)
└── Scripts/
    └── TelemetriaBootstrap.cs  # mismo archivo que Android (compartido)
```

### Configuración del Plugin Importer (`telemetria.dll`)

Selecciona `Assets/Plugins/x86_64/telemetria.dll` e inspecciónalo en el Inspector:

- **Uncheck "Any Platform"** (debe quedar desmarcado).
- Marca la plataforma **Standalone** y, dentro de ella, **64-bit**.
- Desmarca CPU **ARM** / **x86** (32-bit); deja solo **x86_64**.
- No actives iOS/Android/Linux/macOS para esta DLL.

> Nota: `TelemetriaAPI.cs` declara los P/Invoke de todas las funciones; en Windows la
> función `telemetry_set_java_context` no existe en la DLL (es exclusiva de Android) y
> está envuelta en `#if UNITY_ANDROID`, por lo que ni se declara ni se llama.

## 3. Configuración del proyecto (OpenXR)

1. `Package Manager` → instala **OpenXR Plug-in Management** y **OpenXR Hands**.
2. `Edit > Project Settings > XR Plug-in Management` → pestaña **PC** → habilita **OpenXR**.
3. En `OpenXR` (proyecto) habilita el feature **Hands** (OpenXR Hands).
4. `Project Settings > Player` → **Api Compatibility Level .NET 4.x**, arquitectura **x86_64**.
5. SteamVR instalado y en ejecución como runtime OpenXR.

**Hand tracking es OPCIONAL en runtime:** si el dispositivo/runtime no expone
hand tracking, el bootstrap registra joints = 0 y sigue capturando HMD + mandos sin error
(solo se emite un log de aviso si `handTracking=true` en el config y no hay `XRHandSubsystem`).

## 4. `initialConfig.json`

El nativo lee `<baseDir>/initialConfig.json`, donde `baseDir = Application.dataPath`
(la carpeta `Assets/` del proyecto o la carpeta de datos junto al `.exe` en un build).

- **Editor:** colócalo en la carpeta `Assets/` del proyecto.
- **Build:** colócalo en la carpeta de datos del build (`<AppName>_Data/`), junto al `.exe`.

Si la instalación es de solo lectura (p. ej. `Program Files`) y falla la escritura de
`session_<id>.c3d` junto a `dataPath`, usa `Application.persistentDataPath` como
`baseDir` alternativa (cambia la línea `Marshal.StringToHGlobalAnsi(Application.dataPath)`
por `Application.persistentDataPath`); la lectura del config seguirá funcionando desde
dicha ruta. Esquema JSON sin cambios respecto a Android (ver README principal).

## 5. Verificación

1. Consola de Unity: `telemetry_initialize rc=<frameRate>` (rc entre 1 y 240).
2. Aparece `session_<id>.c3d` en `baseDir`.
3. Los POST JSON llegan al `endpointUrl` configurado.

Requiere un PC-VR con SteamVR en marcha (no verificable en CI).
