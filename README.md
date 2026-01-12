# LibreMotion_Capture

LibreMotion_Capture is a native C++ library developed for Android whose purpose is to capture motion data from Virtual Reality devices, including the HMD, controllers, and hand tracking, from applications developed in Unity and Unreal Engine.

The library is part of the LibreMotion ecosystem and is designed to facilitate the acquisition, normalization, and persistence of motion data for biomechanical analysis, research, or telemetry purposes.

---

## Repository contents

This repository includes:

- Native C++ core, compilable as a shared library (.so), located in the /cpp folder  
- Android wrapper packaged as an AAR for use in Unity, located in /LibreriaTelemetria  
- Scripts for using the library in Unity  
- Plugin for using the library in Unreal Engine  
- Example of the initial configuration JSON file `initialConfig.json`  
- Scripts and configuration required for compilation  

---

## Requirements

- Android NDK  
- CMake  
- Ninja  
- Gradle  

Note: the current build workflow targets Android arm64-v8a with API level 24.

---

## Native library (.so) compilation

### 1. Configure the NDK path

In this example, the NDK bundled with Unity is used. Adjust the path if your Unity version is different.

```powershell
$NDK = "C:/Program Files/Unity/Hub/Editor/6000.2.5f1/Editor/Data/PlaybackEngines/AndroidPlayer/NDK"
```

### 2. Generate build files with CMake

From the folder containing the source files (.cpp):

```powershell
cmake -S . -B build-arm64 -G "Ninja" ^
  -D CMAKE_BUILD_TYPE=Release ^
  -D CMAKE_TOOLCHAIN_FILE="%NDK%/build/cmake/android.toolchain.cmake" ^
  -D ANDROID_ABI=arm64-v8a ^
  -D ANDROID_PLATFORM=24 ^
  -D ANDROID_STL=c++_static ^
  -D ANDROID_ARM_NEON=ON
```

### 3. Build the library

```powershell
cmake --build build-arm64 --config Release
```

### 4. Build output

The following file will be generated:

```
build-arm64/libreriaTelemetria.so
```

This file must be manually copied to the following path in order to generate the AAR:

```
LibreriaTelemetria/AARTelemetria/src/main/jniLibs/arm64-v8a/
```

---

## AAR generation (Android Library)

### 1. Required files

To correctly generate the AAR, the following files and paths must exist:

In `AARTelemetria/src/`:
- build.gradle  
- AndroidManifest.xml  

In `AARTelemetria/src/main/`:
- consumer-rules.pro  

In `AARTelemetria/src/main/java/io/github/migueldulu/telemetria/`:
- AyudanteHttp.java  

In the project root (`LibreriaTelemetria/`):
- settings.gradle  
- build.gradle (empty, only required if Gradle produces errors)  

### 2. Generate the Gradle wrapper (if it does not exist)

From the `LibreriaTelemetria` folder:

```powershell
gradle wrapper --gradle-version 8.7 --distribution-type all
```

### 3. Build the AAR

```powershell
.\gradlew.bat :AARTelemetria:assembleDebug
```

### 4. Output

The AAR file will be generated in:

```
AARTelemetria/build/outputs/aar/
```

---

## License

This project is part of the LibreMotion ecosystem and is distributed as open-source software.  
The final license will be added in future versions.
