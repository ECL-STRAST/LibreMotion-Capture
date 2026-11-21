# LibreMotion_Capture

LibreMotion_Capture es una libreria nativa desarrollada en C++ para Android cuyo objetivo es capturar datos de movimiento de dispositivos de Realidad Virtual, incluyendo HMD, controladores y seguimiento de manos, desde aplicaciones desarrolladas en Unity y Unreal Engine.

La libreria forma parte del ecosistema LibreMotion y esta disenada para facilitar la adquisicion, normalizacion y persistencia de datos de movimiento con fines de analisis biomecanico, investigacion o telemetria.

> Este README tambien esta disponible en [ingles (README.md)](README.md).

---

## Plataformas

La libreria se compila desde un unico codigo para dos destinos, compartiendo el mismo formato
JSON, el modelo de marcadores C3D, los flags de funcionalidad y las llamadas de ciclo de vida:

- **Android / Meta Quest** — `libtelemetria.so` + AAR. Comportamiento sin cambios; config leido
  desde `/sdcard/Android/data/<pkg>/files/`. Ver `Unity_Implementation/README_Unity.md` y
  `Unreal_Implementation/README_Unreal.md`.
- **Windows x64 / PC-VR (SteamVR)** — `telemetria.dll` (+ `telemetria.lib` con MSVC), capturado
  mediante los plug-ins **OpenXR** de los motores (Unity: OpenXR + OpenXR Hands; Unreal: OpenXR
  con el runtime SteamVR). El seguimiento de manos es opcional; HMD + mandos funcionan sin el.
  Ver **[docs/steamvr-windows.md](docs/steamvr-windows.md)** (compilacion, ubicaciones del config,
  verificacion y esquema), junto con `Unity_Implementation/README_Unity_Windows.md` y
  `Unreal_Implementation/README_Unreal_Windows.md`.

> [!WARNING]
> **Windows: usa rutas de carpeta solo ASCII.** La carpeta del proyecto/instalacion donde se
> lee `initialConfig.json` y se escribe el `.c3d` (Unity: `Application.dataPath`; Unreal:
> `<Proyecto>/Saved/`) no puede contener tildes, enes ni otros caracteres no ASCII (p. ej.
> `C:\Users\Martín\...`). Si los contiene, el config se ignora sin avisar y no se genera
> ningun `.c3d`. Ver [docs/steamvr-windows.md §7](docs/steamvr-windows.md#7-behaviour-notes).

Los comandos de compilacion nativa de cada destino estan en **[cpp/README-build.md](cpp/README-build.md)**.

### API en C

Declarada en `cpp/core/api/TelemetryAPI.h` (bindings de C#: `Unity_Implementation/Plugins/telemetria/TelemetriaAPI.cs`):

| Funcion | Uso |
| --- | --- |
| `telemetry_set_java_context(vm, activity)` | Solo Android, antes de `telemetry_initialize`: habilita las subidas a traves del AAR. |
| `telemetry_set_frame_rate(hz)` | Antes de `telemetry_initialize`: `hz` en [1, 240] sustituye el `frameRate` de `initialConfig.json` (p. ej. por la frecuencia del visor), `0` anula la sustitucion. Se mantiene entre sesiones. Ver [Unity_Implementation/HMD_REFRESH_RATE.md](Unity_Implementation/HMD_REFRESH_RATE.md). |
| `telemetry_initialize(cfg)` | Inicia una sesion. Devuelve la frecuencia de muestreo (1-240), la misma que se escribe en el C3D, o un error negativo. |
| `telemetry_get_feature_flags()` | Bitmask de los campos opcionales activados en el config. |
| `telemetry_record_frame(frame)` | Graba un frame (C3D + buffer JSON). |
| `telemetry_record_frame_ex(frame, validity)` | Igual, mas el estado de seguimiento del frame (`VRFrameValidityPlain`: pose del casco presente, flags por dispositivo) para el C3D. `validity = NULL` graba exactamente como `telemetry_record_frame`. |
| `telemetry_force_upload()` | Sube ya el chunk JSON pendiente. |
| `telemetry_shutdown()` | Escribe el fichero C3D y detiene el uploader. |


### Estructura del C3D

Un fichero por sesion (`session_<sessionId>.c3d`). Ejes de marcha (X adelante, Y izquierda, Z
arriba), milimetros, float32, una muestra analogica por frame. Puntos (59): `HEAD`, `LFHD`,
`RFHD`, 26 articulaciones por mano (`LFIN` / `RFIN` = posicion del mando), `LWRA`, `LWRB`, `RWRA`,
`RWRB`; residual -1 = ausente. `HEAD`, `LFHD` y `RFHD` solo se escriben como ausentes si el frame
se grabo con `telemetry_record_frame_ex` y `hmdHasPose == 0`. Canales analogicos (218): 0-211
cuaterniones (`HMD_q*`, `L<art>_q*`, `R<art>_q*`), 212 `RealTime` (float32), 213 `RealTime_s`
(segundos enteros), 214 `RealTime_frac` (resto; la suma es el tiempo con precision de ~1e-7 s),
215-217 `HMD_flags`, `L_flags`, `R_flags` (bits de la aplicacion, exactos hasta 24 bits; -1 si no
se informaron). Los canales 0-212 conservan etiqueta e indice; detalles en `README.md`.

---

## Contenido del repositorio

Este repositorio incluye:

- Nucleo nativo en C++ compilable como libreria compartida (.so) en la carpeta /cpp
- Wrapper Android empaquetado como AAR para su uso en Unity en /LibreriaTelemetria
- Scripts para el uso de la libreria en Unity
- Plug in para el uso de la libreria en Unreal Engine
- Ejemplo de JSON de configuración inicial `initialConfig.json`
- Scripts y configuracion necesarios para la compilacion

---

## Requisitos

- Android NDK
- CMake
- Ninja
- Gradle

Nota: el flujo de compilacion actual esta orientado a Android arm64-v8a con API 24.

---

## Compilacion de la libreria nativa (.so)

### 1. Configurar la ruta del NDK

En este ejemplo se utiliza el NDK incluido con Unity. Ajusta la ruta si tu version de Unity es distinta.

```powershell
$NDK = "C:/Program Files/Unity/Hub/Editor/6000.2.5f1/Editor/Data/PlaybackEngines/AndroidPlayer/NDK"
```

### 2. Generar los archivos de build con CMake

Desde la carpeta que contiene los archivos fuente (.cpp):

```powershell
cmake -S . -B build-arm64 -G "Ninja" ^
  -D CMAKE_BUILD_TYPE=Release ^
  -D CMAKE_TOOLCHAIN_FILE="%NDK%/build/cmake/android.toolchain.cmake" ^
  -D ANDROID_ABI=arm64-v8a ^
  -D ANDROID_PLATFORM=24 ^
  -D ANDROID_STL=c++_static ^
  -D ANDROID_ARM_NEON=ON
```

### 3. Compilar la libreria

```powershell
cmake --build build-arm64 --config Release
```

### 4. Resultado de la compilacion

Se generara el archivo:

```
build-arm64/libreriaTelemetria.so
```

Este archivo debe copiarse manualmente a la siguiente ruta para generar el AAR:

```
LibreriaTelemetria/AARTelemetria/src/main/jniLibs/arm64-v8a/
```

---

## Generacion del AAR (Android Library)

### 1. Archivos necesarios

Para generar correctamente el AAR, deben existir los siguientes archivos y rutas:

En `AARTelemetria/src/`:
- build.gradle
- AndroidManifest.xml

En `AARTelemetria/src/main/`:
- consumer-rules.pro

En `AARTelemetria/src/main/java/io/github/migueldulu/telemetria/`:
- AyudanteHttp.java

En la raiz del proyecto (`LibreriaTelemetria/`):
- settings.gradle
- build.gradle (vacio, solo necesario si Gradle produce errores)

### 2. Generar el wrapper de Gradle (si no existe)

Desde la carpeta `LibreriaTelemetria`:

```powershell
gradle wrapper --gradle-version 8.7 --distribution-type all
```

### 3. Compilar el AAR

```powershell
.\gradlew.bat :AARTelemetria:assembleDebug
```

### 4. Resultado

El archivo AAR se generara en:

```
AARTelemetria/build/outputs/aar/
```

---

## Licencia

Este proyecto forma parte del ecosistema LibreMotion y se distribuye como software open-source.
La licencia definitiva se anadira en futuras versiones.
