# SteamVR / Windows Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor LibreMotion-Capture into one cross-platform C++ core that builds a Windows x64 `telemetria.dll` (SteamVR/PC-VR) and the existing Android `.so`/AAR, with identical JSON + C3D output, plus updated Unity and Unreal integrations.

**Architecture:** Platform-agnostic core (`cpp/core/`) depends on three injected interfaces — `IHttpTransport`, `IPathResolver`, and `ILogSink` — with per-platform implementations under `cpp/platform/{android,windows}`. A single CMake build produces a STATIC `telemetria_core` (unit-tested with GoogleTest) and a SHARED `telemetria` per platform. Engines keep driving OpenXR and pass plain structs into an unchanged C ABI (extended additively with `baseDir`).

**Tech Stack:** C++17, CMake ≥ 3.22, Ninja, GoogleTest (existing submodule at `cpp/thirdparty/ezc3d/external/gtest`), ezc3d (existing submodule), WinHTTP (Windows), JNI + `AyudanteHttp` (Android), Unity `UnityEngine.XR` + `UnityEngine.XR.Hands`, Unreal `HeadMountedDisplay`.

**Spec:** `docs/superpowers/specs/2026-09-14-steamvr-windows-port-design.md`

## Global Constraints

- License stays MIT (unchanged). C++ standard is C++17 (verbatim: `set(CMAKE_CXX_STANDARD 17)`).
- The JSON wire format, the C3D point/analog layout + naming, the OpenXR→Gait axis conversion (meters→mm), the 26-joint hand tags, the trailing `RealTime` channel, the feature-flag bitmask (bit0 hand, bit1 primary, bit2 secondary, bit3 grip, bit4 trigger, bit5 joystick), and the `telemetry_initialize` return contract (effective frameRate, clamp to [1,240], default 60) MUST stay byte-for-byte identical to the current Android behavior.
- Windows HTTP uses WinHTTP only (no libcurl / cpp-httplib). Android keeps JNI→`AyudanteHttp`.
- Platform specifics are behind interfaces + per-platform impls; NO inline `#ifdef _WIN32/__ANDROID__` inside `cpp/core/`.
- Hand tracking is optional at runtime; HMD + controllers must fully work when hands are absent.
- One shared core builds BOTH targets; the C ABI function names do not change (only additive struct field `baseDir`).
- Tests run on the host / CI without any VR runtime; live VR capture is verified manually.

---

## File Structure (final layout)

```
cpp/
  core/
    VRTypes.h                (moved from cpp/VRTypes.h)
    TelemetryTypes.h         (moved from cpp/TelemetryTypes.h; add baseDir to TelemetryConfigPlain)
    platform/Interfaces.h    (IHttpTransport, IPathResolver, ILogSink, LogLevel)   [new]
    platform/Log.h           (lmc::Log::set/info/warn/err + readFileToString decl)  [new]
    platform/Log.cpp         (default stderr sink + implementation)                 [new]
    platform/FileUtil.cpp    (readFileToString portable impl)                       [new]
    config/ConfigParser.h    (pure text -> UploaderConfig, flags, frameRate)        [new]
    config/ConfigParser.cpp                                                            [new]
    TelemetryManager.h/.cpp  (moved; depends on IHttpTransport* + lmc::Log)
    C3DRecorder.h/.cpp       (moved; depends on IPathResolver + lmc::Log)
    api/TelemetryAPI.h       (moved C ABI header; adds baseDir + Android-only decls)[new content]
    api/TelemetryAPI.cpp     (moved; wires PlatformBundle by platform selection)
  platform/
    android/
      AndroidLogSink.h/.cpp          (android log sink)            [new]
      AndroidPathResolver.h/.cpp     (/sdcard + /proc/self/cmdline)[new]
      AndroidHttpTransport.h/.cpp    (JNI -> AyudanteHttp)         [new, moved from JSONUploader]
      AndroidJavaContext.h/.cpp      (JNI_OnLoad + set_java_context)[new, moved from TelemetryAPI]
      AndroidPlatformBundle.cpp      (builds the 3 impls)          [new]
    windows/
      WinLogSink.h/.cpp              (OutputDebugString + stderr)  [new]
      WinPathResolver.h/.cpp         (explicit baseDir)           [new]
      WinHttpTransport.h/.cpp        (WinHTTP POST + BuildHeaders)[new]
      WinPlatformBundle.cpp          (builds the 3 impls)         [new]
  tests/
    MockPlatform.h/.cpp              (mock IHttpTransport, temp-dir IPathResolver, recording ILogSink) [new]
    test_config_parser.cpp          [new]
    test_path_resolver_win.cpp      [new]
    test_manager.cpp                [new]
    test_c3d_recorder.cpp           [new]
    test_http_headers.cpp           [new]
    main.cpp                        [new, gtest main]
  CMakeLists.txt                    (rewritten: core STATIC + shared telemetria + tests)
```

Notes:
- The old flat files (`cpp/JSONUploader.*`, `cpp/configReader.*`) are removed; their logic is split into `core/config/ConfigParser.*` (pure parsing), the platform path resolvers, and the platform HTTP transports. `cpp/TelemetryManager.*`, `cpp/C3DRecorder.*`, `cpp/VRTypes.h`, `cpp/TelemetryTypes.h`, `cpp/TelemetryAPI.*` move into `cpp/core/...`.
- GoogleTest is added via the existing submodule: `add_subdirectory(thirdparty/ezc3d/external/gtest)` (only in the host/test build, never for Android).

---

## Task 1: Build scaffold — static core + GoogleTest harness

Establishes the new `core/` static library and the GoogleTest test binary so every later task has a place to put a running test. No behavior change yet; the existing Android build must still configure.

**Files:**
- Create: `cpp/core/platform/Interfaces.h`
- Create: `cpp/core/platform/Log.h`
- Create: `cpp/core/platform/Log.cpp`
- Create: `cpp/tests/main.cpp`
- Modify: `cpp/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `lmc::LogLevel` enum `{ kInfo = 0, kWarn = 1, kError = 2 }`.
  - `struct lmc::ILogSink { virtual ~ILogSink(); virtual void log(int level, const char* msg) = 0; };`
  - `namespace lmc::Log { void set(lmc::ILogSink* sink); void info(const char* fmt, ...); void warn(const char* fmt, ...); void err(const char* fmt, ...); }`
  - CMake targets: `telemetria_core` (STATIC), `telemetria_tests` (GoogleTest exe).

- [ ] **Step 1: Create `cpp/core/platform/Interfaces.h`**

```cpp
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace lmc {

enum LogLevel { kInfo = 0, kWarn = 1, kError = 2 };

struct ILogSink {
    virtual ~ILogSink() = default;
    virtual void log(int level, const char* msg) = 0;
};

// Sends one already-serialized JSON body to the configured endpoint.
// Returns true only on HTTP 2xx. Must be callable from a background thread.
struct IHttpTransport {
    virtual ~IHttpTransport() = default;
    virtual bool postJson(const std::string& url,
                          const std::string& body,
                          const std::vector<std::pair<std::string, std::string>>& headers) = 0;
};

// Locates the initial config file and the per-session C3D output path.
struct IPathResolver {
    virtual ~IPathResolver() = default;
    virtual bool configFilePath(std::string& out) = 0;
    virtual bool c3dFilePath(const std::string& sessionId, std::string& out) = 0;
};

// Bundle of the three platform impls, built once at initialize().
struct PlatformBundle {
    IHttpTransport* http   = nullptr;   // owned by the platform layer (static storage)
    IPathResolver*  paths  = nullptr;
    ILogSink*       log    = nullptr;
};

// Implemented by each platform's *PlatformBundle.cpp; returns static-storage impls.
PlatformBundle buildPlatformBundle(const char* baseDir);

} // namespace lmc
```

- [ ] **Step 2: Create `cpp/core/platform/Log.h` and `cpp/core/platform/Log.cpp`**

`Log.h`:
```cpp
#pragma once
#include <string>
#include "Interfaces.h"

namespace lmc {
namespace Log {
    void set(ILogSink* sink);                 // install sink (nullptr => default stderr sink)
    void info(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void err (const char* fmt, ...);
}
bool readFileToString(const std::string& path, std::string& outText);
} // namespace lmc
```

`Log.cpp`:
```cpp
#include "Log.h"
#include <cstdarg>
#include <cstdio>

namespace lmc {

// Default sink writes to stderr; used before a platform installs its own and in tests.
class StderrSink : public ILogSink {
public:
    void log(int level, const char* msg) override {
        const char* tag = (level >= kError) ? "ERROR" : (level == kWarn ? "WARN" : "INFO");
        std::fprintf(stderr, "[telemetria][%s] %s\n", tag, msg);
    }
};

static ILogSink* g_sink = nullptr;

static ILogSink* sink() {
    static StderrSink fallback;
    return g_sink ? g_sink : &fallback;
}

namespace Log {
void set(ILogSink* s) { g_sink = s; }

static void emit(int level, const char* fmt, va_list ap) {
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    sink()->log(level, buf);
}

void info(const char* fmt, ...) { va_list a; va_start(a, fmt); emit(kInfo, fmt, a); va_end(a); }
void warn(const char* fmt, ...) { va_list a; va_start(a, fmt); emit(kWarn, fmt, a); va_end(a); }
void err (const char* fmt, ...) { va_list a; va_start(a, fmt); emit(kError, fmt, a); va_end(a); }
} // namespace Log

} // namespace lmc
```

- [ ] **Step 3: Create `cpp/tests/main.cpp`**

```cpp
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

- [ ] **Step 4: Rewrite `cpp/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(telemetria LANGUAGES C CXX)

option(TELEMETRIA_BUILD_TESTS "Build GoogleTest unit tests (host only)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ezc3d: always static, never its tests
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(EZC3D_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(EZC3D_BUILD_STATIC ON  CACHE BOOL "" FORCE)
set(EZC3D_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/ezc3d)

# ---- Platform sources selection (no #ifdef inside core .cpp) ----
if(ANDROID)
    set(TELEMETRIA_PLATFORM_SOURCES
        platform/android/AndroidLogSink.cpp
        platform/android/AndroidPathResolver.cpp
        platform/android/AndroidHttpTransport.cpp
        platform/android/AndroidJavaContext.cpp
        platform/android/AndroidPlatformBundle.cpp)
elseif(WIN32)
    set(TELEMETRIA_PLATFORM_SOURCES
        platform/windows/WinLogSink.cpp
        platform/windows/WinPathResolver.cpp
        platform/windows/WinHttpTransport.cpp
        platform/windows/WinPlatformBundle.cpp)
else()
    set(TELEMETRIA_PLATFORM_SOURCES "")
endif()

# ---- Shared core sources (platform-agnostic) ----
set(TELEMETRIA_CORE_SOURCES
    core/api/TelemetryAPI.cpp
    core/TelemetryManager.cpp
    core/C3DRecorder.cpp
    core/config/ConfigParser.cpp
    core/platform/Log.cpp
    core/platform/FileUtil.cpp)

# STATIC core used by both the shared lib and the tests.
add_library(telemetria_core STATIC ${TELEMETRIA_CORE_SOURCES})
target_include_directories(telemetria_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(telemetria_core PUBLIC ezc3d)

# SHARED telemetry library (engine-facing C ABI).
if(TELEMETRIA_PLATFORM_SOURCES)
    add_library(telemetria SHARED ${TELEMETRIA_PLATFORM_SOURCES})
    target_link_libraries(telemetria PRIVATE telemetria_core)
    if(ANDROID)
        target_link_libraries(telemetria PRIVATE log)
    elseif(WIN32)
        target_link_libraries(telemetria PRIVATE winhttp)
    endif()
endif()

# ---- Tests (host only; never on Android) ----
if(TELEMETRIA_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_subdirectory(thirdparty/ezc3d/external/gtest ${CMAKE_BINARY_DIR}/gtest)
    add_executable(telemetria_tests
        tests/main.cpp
        tests/MockPlatform.cpp
        tests/test_config_parser.cpp
        tests/test_path_resolver_win.cpp
        tests/test_manager.cpp
        tests/test_c3d_recorder.cpp
        tests/test_http_headers.cpp)
    target_link_libraries(telemetria_tests PRIVATE telemetria_core gtest gtest_main)
    target_include_directories(telemetria_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/windows)
    add_test(NAME telemetria_tests COMMAND telemetria_tests)
endif()
```

- [ ] **Step 5: Create a temporary empty `cpp/core/api/TelemetryAPI.cpp`, `cpp/core/TelemetryManager.cpp`, `cpp/core/C3DRecorder.cpp`, `cpp/core/config/ConfigParser.cpp`, `cpp/core/platform/FileUtil.cpp` with `// filled in later tasks` so the STATIC lib compiles** (Tasks 2–9 replace them; Task 1 only needs the target graph to configure, so a stub that compiles is enough).

- [ ] **Step 6: Create stub test files that compile**: `cpp/tests/MockPlatform.h`, `cpp/tests/MockPlatform.cpp`, and the five `test_*.cpp` each containing `#include <gtest/gtest.h>` and a single `TEST(Smoke, Builds) { EXPECT_TRUE(true); }` for now (later tasks fill them in).

- [ ] **Step 7: Configure + build the test target on the host to verify the graph**

Run: `cmake -S cpp -B cpp/build-host -G "Ninja" -DTELEMETRIA_BUILD_TESTS=ON && cmake --build cpp/build-host --target telemetria_tests`
Expected: configures and compiles; `telemetria_tests` links.

Run: `cpp/build-host/telemetria_tests`
Expected: PASS (Smoke tests).

- [ ] **Step 8: Commit**

```bash
git add cpp/CMakeLists.txt cpp/core cpp/tests
git commit -m "chore: scaffold cross-platform core static lib + GoogleTest harness"
```

---

## Task 2: Extract pure `ConfigParser` from `configReader.cpp`

Move the hand-rolled JSON parsing (keys/defaults/flags/frameRate) into a pure function that takes text, with no file IO and no path logic. This is fully unit-testable.

**Files:**
- Create: `cpp/core/config/ConfigParser.h`
- Modify: `cpp/core/config/ConfigParser.cpp` (replace stub)
- Test: `cpp/tests/test_config_parser.cpp`

**Interfaces:**
- Consumes: `UploaderConfig` (from `TelemetryTypes.h` — moved in Task 9; for now include the current `TelemetryTypes.h` via the `cpp/` include dir already on the target).
- Produces (namespace `lmc`):
  - `void configSetDefaults(UploaderConfig& cfg);` — sets endpoint `http://localhost:1414`, apiKey `non_existant`, framesPerFile 200, all flags false.
  - `bool configParseJson(const std::string& text, UploaderConfig& cfg);` — overrides keys found in `text`; returns true if parsed (even with missing keys). Does NOT reset to defaults first.
  - `int configParseFrameRate(const std::string& text, int fallback /*60*/);` — reads `"frameRate"`, clamps to [1,240], else fallback.
  - `unsigned configFeatureFlagsBitmask(const UploaderConfig& cfg);` — bit0 hand, bit1 primary, bit2 secondary, bit3 grip, bit4 trigger, bit5 joystick.
- Constants (exact, verbatim from current defaults): `kDefaultEndpoint="http://localhost:1414"`, `kDefaultApiKey="non_existant"`, `kDefaultFramesPerFile=200`, `kDefaultFrameRate=60`.

- [ ] **Step 1: Write the failing test** — `cpp/tests/test_config_parser.cpp`

```cpp
#include <gtest/gtest.h>
#include "config/ConfigParser.h"
#include "TelemetryTypes.h"

using namespace lmc;

TEST(ConfigParser, DefaultsApplied) {
    UploaderConfig cfg;
    configSetDefaults(cfg);
    EXPECT_EQ(cfg.endpointUrl, "http://localhost:1414");
    EXPECT_EQ(cfg.apiKey, "non_existant");
    EXPECT_EQ(cfg.framesPerFile, 200);
    EXPECT_FALSE(cfg.handTracking);
}

TEST(ConfigParser, OverridesAndFlags) {
    const char* json = R"({
      "endpoint":"https://x/tb","apiKey":"K","framesPerFile":150,
      "handTracking":true,"primaryButton":true,"secondaryButton":true,
      "grip":true,"trigger":true,"joystick":true
    })";
    UploaderConfig cfg; configSetDefaults(cfg);
    ASSERT_TRUE(configParseJson(json, cfg));
    EXPECT_EQ(cfg.endpointUrl, "https://x/tb");
    EXPECT_EQ(cfg.apiKey, "K");
    EXPECT_EQ(cfg.framesPerFile, 150);
    EXPECT_TRUE(cfg.handTracking);
    EXPECT_EQ(configFeatureFlagsBitmask(cfg), 0b111111u);
}

TEST(ConfigParser, FrameRateClampAndFallback) {
    EXPECT_EQ(configParseFrameRate(R"({"frameRate":72})", 60), 72);
    EXPECT_EQ(configParseFrameRate(R"({"frameRate":900})", 60), 60);
    EXPECT_EQ(configParseFrameRate(R"({})", 60), 60);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build cpp/build-host --target telemetria_tests && cpp/build-host/telemetria_tests --gtest_filter=ConfigParser.*`
Expected: FAIL — `ConfigParser.h` / functions not found (compile error).

- [ ] **Step 3: Write `cpp/core/config/ConfigParser.h`**

```cpp
#pragma once
#include <string>
#include "TelemetryTypes.h"

namespace lmc {
void     configSetDefaults(UploaderConfig& cfg);
bool     configParseJson(const std::string& text, UploaderConfig& cfg);
int      configParseFrameRate(const std::string& text, int fallback = 60);
unsigned configFeatureFlagsBitmask(const UploaderConfig& cfg);
} // namespace lmc
```

- [ ] **Step 4: Implement `cpp/core/config/ConfigParser.cpp`** by moving the `extractJsonString/Int/Bool` helpers and the parsing/defaults logic verbatim from the current `cpp/configReader.cpp` into the `lmc` namespace, exposing the four functions above. Reuse the existing helper bodies exactly (same escape handling, same key names).

```cpp
#include "config/ConfigParser.h"
#include <cctype>

namespace {
// --- moved verbatim from cpp/configReader.cpp (anonymous namespace) ---
bool extractJsonString(const std::string& text, const std::string& key, std::string& out);
bool extractJsonInt  (const std::string& text, const std::string& key, int& out);
bool extractJsonBool (const std::string& text, const std::string& key, bool& out);
}

namespace lmc {
static constexpr const char* kDefaultEndpoint     = "http://localhost:1414";
static constexpr const char* kDefaultApiKey       = "non_existant";
static constexpr int         kDefaultFramesPerFile = 200;
static constexpr int         kDefaultFrameRate     = 60;

void configSetDefaults(UploaderConfig& c) {
    c.endpointUrl = kDefaultEndpoint; c.apiKey = kDefaultApiKey;
    c.framesPerFile = kDefaultFramesPerFile;
    c.handTracking = c.primaryButton = c.secondaryButton = false;
    c.grip = c.trigger = c.joystick = false;
}
bool configParseJson(const std::string& t, UploaderConfig& c) {
    std::string s = c.endpointUrl, k = c.apiKey; int f = c.framesPerFile, vi; bool vb;
    if (extractJsonString(t, "endpoint", s) && !s.empty()) c.endpointUrl = s;
    if (extractJsonString(t, "apiKey", k)   && !k.empty()) c.apiKey = k;
    if (extractJsonInt(t, "framesPerFile", vi) && vi > 0)  c.framesPerFile = vi;
    if (extractJsonBool(t, "handTracking", vb))    c.handTracking = vb;
    if (extractJsonBool(t, "primaryButton", vb))   c.primaryButton = vb;
    if (extractJsonBool(t, "secondaryButton", vb)) c.secondaryButton = vb;
    if (extractJsonBool(t, "grip", vb))    c.grip = vb;
    if (extractJsonBool(t, "trigger", vb)) c.trigger = vb;
    if (extractJsonBool(t, "joystick", vb))c.joystick = vb;
    return true;
}
int configParseFrameRate(const std::string& t, int fallback) {
    int vi;
    if (extractJsonInt(t, "frameRate", vi) && vi >= 1 && vi <= 240) return vi;
    return fallback;
}
unsigned configFeatureFlagsBitmask(const UploaderConfig& c) {
    unsigned m = 0;
    if (c.handTracking)   m |= 1u << 0;
    if (c.primaryButton)  m |= 1u << 1;
    if (c.secondaryButton)m |= 1u << 2;
    if (c.grip)           m |= 1u << 3;
    if (c.trigger)        m |= 1u << 4;
    if (c.joystick)       m |= 1u << 5;
    return m;
}
} // namespace lmc
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build cpp/build-host --target telemetria_tests && cpp/build-host/telemetria_tests --gtest_filter=ConfigParser.*`
Expected: PASS (3 tests).

- [ ] **Step 6: Commit**

```bash
git add cpp/core/config cpp/tests/test_config_parser.cpp
git commit -m "feat(core): extract pure ConfigParser with unit tests"
```

---

## Task 3: `IPathResolver` implementations (Windows + Android)

Implement the Windows base-dir resolver (testable on host) and the Android `/sdcard` + `/proc/self/cmdline` resolver (moved from `configReader.cpp`). Move `readFileToString` into `core/platform/FileUtil.cpp` (portable).

**Files:**
- Create: `cpp/core/platform/FileUtil.cpp`
- Create: `cpp/platform/windows/WinPathResolver.h`, `cpp/platform/windows/WinPathResolver.cpp`
- Create: `cpp/platform/android/AndroidPathResolver.h`, `cpp/platform/android/AndroidPathResolver.cpp`
- Test: `cpp/tests/test_path_resolver_win.cpp`

**Interfaces:**
- Produces:
  - `std::string lmc::joinPath(const std::string& a, const std::string& b);` (in `FileUtil.cpp`, decl in `Log.h` or a new `platform/PathUtil.h`) — joins with `/`, avoiding double separators.
  - `std::string lmc::sanitizeSessionId(const std::string& raw);` — same rules as current `C3DRecorder::sanitizeSessionId` (alnum/-/_ else `_`; empty → `"unknown"`).
  - `lmc::WinPathResolver::WinPathResolver(std::string baseDir)` implementing `IPathResolver`: `configFilePath` = `baseDir/initialConfig.json`; `c3dFilePath` = `baseDir/session_<sanitized>.c3d`.
  - `lmc::AndroidPathResolver` implementing `IPathResolver`: config path `/sdcard/Android/data/<pkg>/files/initialConfig.json`, C3D in that same dir; package from `/proc/self/cmdline`.
- Consumes: `IPathResolver` (Task 1).

- [ ] **Step 1: Write the failing Windows test** — `cpp/tests/test_path_resolver_win.cpp`

```cpp
#include <gtest/gtest.h>
#include "windows/WinPathResolver.h"

using namespace lmc;

TEST(WinPathResolver, ConfigAndC3dPaths) {
    WinPathResolver r("C:/Saved");
    std::string cfg, c3d;
    ASSERT_TRUE(r.configFilePath(cfg));
    EXPECT_EQ(cfg, "C:/Saved/initialConfig.json");
    ASSERT_TRUE(r.c3dFilePath("App-2026-01-01-abc", c3d));
    EXPECT_EQ(c3d, "C:/Saved/session_App-2026-01-01-abc.c3d");
}

TEST(WinPathResolver, SanitizesSessionId) {
    WinPathResolver r("D:/x");
    std::string c3d;
    ASSERT_TRUE(r.c3dFilePath("bad/name with spaces", c3d));
    EXPECT_EQ(c3d, "D:/x/session_bad_name_with_spaces.c3d");
}
```

- [ ] **Step 2: Run to verify it fails** — `cmake --build cpp/build-host --target telemetria_tests && cpp/build-host/telemetria_tests --gtest_filter=WinPathResolver.*`
Expected: FAIL — header/class not found.

- [ ] **Step 3: Implement `core/platform/FileUtil.cpp`**

```cpp
#include "platform/Log.h"
#include <fstream>
#include <sstream>
#include <cctype>

namespace lmc {
bool readFileToString(const std::string& path, std::string& outText) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return false;
    std::ostringstream ss; ss << in.rdbuf();
    outText = ss.str();
    return true;
}
std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}
std::string sanitizeSessionId(const std::string& raw) {
    std::string s = raw;
    for (char& c : s)
        if (!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_')) c = '_';
    if (s.empty()) s = "unknown";
    return s;
}
} // namespace lmc
```

Add decls to `cpp/core/platform/Log.h` (inside `namespace lmc`): `std::string joinPath(const std::string&, const std::string&); std::string sanitizeSessionId(const std::string&);`

- [ ] **Step 4: Implement `platform/windows/WinPathResolver.{h,cpp}`**

`WinPathResolver.h`:
```cpp
#pragma once
#include <string>
#include "platform/Interfaces.h"
namespace lmc {
class WinPathResolver : public IPathResolver {
public:
    explicit WinPathResolver(std::string baseDir) : base_(std::move(baseDir)) {}
    bool configFilePath(std::string& out) override;
    bool c3dFilePath(const std::string& sessionId, std::string& out) override;
private:
    std::string base_;
};
} // namespace lmc
```

`WinPathResolver.cpp`:
```cpp
#include "windows/WinPathResolver.h"
#include "platform/Log.h"
namespace lmc {
bool WinPathResolver::configFilePath(std::string& out) { if (base_.empty()) return false; out = joinPath(base_, "initialConfig.json"); return true; }
bool WinPathResolver::c3dFilePath(const std::string& sessionId, std::string& out) { if (base_.empty()) return false; out = joinPath(base_, "session_" + sanitizeSessionId(sessionId) + ".c3d"); return true; }
} // namespace lmc
```

- [ ] **Step 5: Implement `platform/android/AndroidPathResolver.{h,cpp}`** by moving `getPackageNameFromProc` + the `/sdcard/Android/data/<pkg>/files/` path construction verbatim from `cpp/configReader.cpp` into the class; `configFilePath` returns that path; `c3dFilePath` returns the same directory + `session_<sanitized>.c3d`. (Not unit-tested on host; verified by the Android build in Task 10.)

- [ ] **Step 6: Run Windows tests to verify pass** — `cmake --build cpp/build-host --target telemetria_tests && cpp/build-host/telemetria_tests --gtest_filter=WinPathResolver.*`
Expected: PASS (2 tests).

- [ ] **Step 7: Commit** — `git add cpp/core/platform/FileUtil.cpp cpp/platform cpp/tests/test_path_resolver_win.cpp && git commit -m "feat: WinPathResolver + AndroidPathResolver + portable FileUtil"`

---

## Task 4: `ILogSink` implementations (Windows + Android)

Provide real log sinks and confirm the core `lmc::Log` dispatches to them.

**Files:**
- Create: `cpp/platform/windows/WinLogSink.h`, `cpp/platform/windows/WinLogSink.cpp`
- Create: `cpp/platform/android/AndroidLogSink.h`, `cpp/platform/android/AndroidLogSink.cpp`
- Test: `cpp/tests/test_manager.cpp` (add one dispatch assertion; full manager suite arrives in Task 7)

**Interfaces:**
- Produces: `lmc::WinLogSink` (uses `OutputDebugStringA`, and mirrors to stderr when a debugger is attached or `TELEMETRIA_CONSOLE` is defined); `lmc::AndroidLogSink` (uses `__android_log_print` with tag `telemetria`).

- [ ] **Step 1: Implement `platform/windows/WinLogSink.{h,cpp}`**

```cpp
// WinLogSink.h
#pragma once
#include "platform/Interfaces.h"
namespace lmc { class WinLogSink : public ILogSink { public: void log(int level, const char* msg) override; }; }
```
```cpp
// WinLogSink.cpp
#include "windows/WinLogSink.h"
#include <windows.h>
#include <cstdio>
namespace lmc {
void WinLogSink::log(int, const char* msg) {
    std::string s = std::string("[telemetria] ") + msg + "\n";
    OutputDebugStringA(s.c_str());
#ifdef TELEMETRIA_CONSOLE
    std::fputs(s.c_str(), stderr);
#endif
}
} // namespace lmc
```

- [ ] **Step 2: Implement `platform/android/AndroidLogSink.{h,cpp}`** mapping `kInfo→ANDROID_LOG_INFO`, `kWarn→ANDROID_LOG_WARN`, `kError→ANDROID_LOG_ERROR` via `__android_log_print(ANDROID_LOG_*, "telemetria", "%s", msg)`.

- [ ] **Step 3: Add a dispatch test to `cpp/tests/test_manager.cpp`**

```cpp
#include <gtest/gtest.h>
#include "platform/Log.h"
#include <vector>
namespace { struct Cap : public lmc::ILogSink { std::vector<std::string> m; std::vector<int> lvl;
    void log(int l, const char* s) override { lvl.push_back(l); m.push_back(s); } }; }
TEST(LogDispatch, RoutesToInstalledSink) {
    Cap c; lmc::Log::set(&c);
    lmc::Log::err("boom %d", 42);
    lmc::Log::set(nullptr);
    ASSERT_EQ(c.m.size(), 1u);
    EXPECT_EQ(c.lvl[0], lmc::kError);
    EXPECT_EQ(c.m[0], "boom 42");
}
```

- [ ] **Step 4: Run** — `cmake --build cpp/build-host --target telemetria_tests && cpp/build-host/telemetria_tests --gtest_filter=LogDispatch.*` → PASS. (The Windows/Android sink `.cpp` files are compiled by the shared-lib target, not the host test exe; the test only exercises the core dispatch.)

- [ ] **Step 5: Commit** — `git add cpp/platform cpp/tests/test_manager.cpp && git commit -m "feat: Windows + Android log sinks"`

---

## Task 5: Windows `IHttpTransport` (WinHTTP) + shared header builder

Extract the Supabase-style header construction into a pure, host-testable helper, then implement WinHTTP transport using it.

**Files:**
- Create: `cpp/platform/windows/WinHttpTransport.h`, `cpp/platform/windows/WinHttpTransport.cpp`
- Test: `cpp/tests/test_http_headers.cpp`

**Interfaces:**
- Produces:
  - `std::vector<std::pair<std::string,std::string>> lmc::buildSupabaseHeaders(const std::string& apiKey);` (declared in `WinHttpTransport.h`, defined in the same `.cpp`) → `{ {"Content-Type","application/json"}, {"apikey",apiKey}, {"Authorization","Bearer "+apiKey}, {"Prefer","return=representation"} }` (exact order/keys from current `JSONUploader::uploadJson`).
  - `lmc::WinHttpTransport : IHttpTransport` — `postJson` returns true only on HTTP 2xx.
- Consumes: `IHttpTransport` (Task 1).

- [ ] **Step 1: Write the failing header test** — `cpp/tests/test_http_headers.cpp`

```cpp
#include <gtest/gtest.h>
#include "windows/WinHttpTransport.h"
using namespace lmc;
TEST(HttpHeaders, MatchesOriginalSet) {
    auto h = buildSupabaseHeaders("K");
    ASSERT_EQ(h.size(), 4u);
    EXPECT_EQ(h[0].first, "Content-Type"); EXPECT_EQ(h[0].second, "application/json");
    EXPECT_EQ(h[1].first, "apikey");       EXPECT_EQ(h[1].second, "K");
    EXPECT_EQ(h[2].first, "Authorization");EXPECT_EQ(h[2].second, "Bearer K");
    EXPECT_EQ(h[3].first, "Prefer");       EXPECT_EQ(h[3].second, "return=representation");
}
```

- [ ] **Step 2: Run to verify fail** — filter `HttpHeaders.*` → compile error (missing).

- [ ] **Step 3: Implement `WinHttpTransport.h`** (class decl + `buildSupabaseHeaders` decl).

- [ ] **Step 4: Implement `WinHttpTransport.cpp`** — `buildSupabaseHeaders` (as above) + `postJson` using WinHTTP:

```cpp
#include "windows/WinHttpTransport.h"
#include "platform/Log.h"
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace lmc {
std::vector<std::pair<std::string,std::string>> buildSupabaseHeaders(const std::string& apiKey) {
    return { {"Content-Type","application/json"}, {"apikey",apiKey},
             {"Authorization","Bearer "+apiKey}, {"Prefer","return=representation"} };
}

bool WinHttpTransport::postJson(const std::string& url, const std::string& body,
                                const std::vector<std::pair<std::string,std::string>>& headers) {
    // 1) crack URL, 2) WinHttpOpen (WINHTTP_ACCESS_TYPE_DEFAULT_PROXY),
    // 3) WinHttpConnect, 4) WinHttpOpenRequest with WINHTTP_FLAG_SECURE when https,
    // 5) WinHttpAddRequestHeaders for each header ("Key: Value\r\n"),
    // 6) WinHttpSendRequest + WinHttpReceiveResponse,
    // 7) query WINHTTP_QUERY_STATUS_CODE, return (code>=200 && code<300),
    // 8) close all handles. On any failure Log::err and return false.
    // Set timeouts: connect 15000ms, read 30000ms (matching AyudanteHttp).
    // ... (full WinHTTP sequence implemented here; ~60 lines, no exceptions) ...
    return false; // replaced by real implementation
}
} // namespace lmc
```
Replace the `// ...` body with the complete WinHTTP call sequence (the implementer writes all calls + handle cleanup); the header-building is what this task's test locks. Keep `-fno-exceptions` compatible (no `throw`, no try/catch).

- [ ] **Step 5: Run header test** — filter `HttpHeaders.*` → PASS.

- [ ] **Step 6: Commit** — `git add cpp/platform/windows/WinHttpTransport.* cpp/tests/test_http_headers.cpp && git commit -m "feat(win): WinHTTP transport + shared Supabase header builder"`

---

## Task 6: Android `IHttpTransport` (JNI→`AyudanteHttp`) + JavaContext

Move the JNI HTTP bridge and the `JNI_OnLoad`/`telemetry_set_java_context` code out of `JSONUploader`/`TelemetryAPI` into the Android platform layer, preserving behavior.

**Files:**
- Create: `cpp/platform/android/AndroidHttpTransport.h`, `cpp/platform/android/AndroidHttpTransport.cpp`
- Create: `cpp/platform/android/AndroidJavaContext.h`, `cpp/platform/android/AndroidJavaContext.cpp`
- Delete: `cpp/JSONUploader.h`, `cpp/JSONUploader.cpp`

**Interfaces:**
- Produces:
  - `lmc::AndroidHttpTransport : IHttpTransport` with `void setJavaContext(JavaVM* vm, jobject activityGlobalRef);` (kept separate so API init injects Java refs before `postJson`).
  - `lmc::android::setJavaContext(JavaVM*, jobject)`, `lmc::android::javaVm()`, `lmc::android::javaActivity()` — global Java state + `JNI_OnLoad` moved into `AndroidJavaContext.cpp`.
- Consumes: `IHttpTransport`.

- [ ] **Step 1: Move the JNI body** from `cpp/JSONUploader.cpp::callJavaMakeRequest` verbatim into `AndroidHttpTransport::postJson`, building the `headers` map as before (headers are now passed in from core via `buildSupabaseHeaders`, so `postJson` no longer constructs them itself). Keep `logJavaException` as a static helper in the new file.

- [ ] **Step 2: Move `JNI_OnLoad` + `telemetry_set_java_context` + the `g_vm`/`g_activity` globals** from `cpp/TelemetryAPI.cpp` into `cpp/platform/android/AndroidJavaContext.cpp`, exposing `lmc::android::*` accessors.

- [ ] **Step 3: Delete `cpp/JSONUploader.{h,cpp}`.** (Reference resolution completes in Task 9.)

- [ ] **Step 4: Verify the Android platform sources still compile in isolation** (syntax/headers) via the Android configure done in Task 10; for this commit, ensure the files are self-consistent and included by `platform/android/*`. No host test (JNI-only).

- [ ] **Step 5: Commit** — `git add -A cpp/platform/android cpp/JSONUploader.* && git commit -m "refactor(android): JNI HTTP + Java context into platform layer"`

---

## Task 7: `TelemetryManager` depends on `IHttpTransport` + `lmc::Log`; add tests

Move `TelemetryManager` into `core/`, replace `JSONUploader*` with `IHttpTransport*`, replace `__android_log_print` with `lmc::Log`, keep `toJsonFlat` output identical. Add buffering/flush/backpressure/drain tests using `MockPlatform`.

**Files:**
- Move+Modify: `cpp/TelemetryManager.{h,cpp}` → `cpp/core/TelemetryManager.{h,cpp}`
- Create: `cpp/tests/MockPlatform.h`, `cpp/tests/MockPlatform.cpp`
- Test: `cpp/tests/test_manager.cpp`

**Interfaces:**
- Consumes: `IHttpTransport` (Task 1), `lmc::Log` (Task 1), `UploaderConfig`/`VRFrameDataPlain`.
- Produces: `class GestorTelemetria` with `bool initialize(const UploaderConfig&, IHttpTransport*)`, `void recordFrame(const VRFrameDataPlain&)`, `void flushAndUpload()`, `void shutdown()`. (Same names; only the second parameter type changes.)
- `MockPlatform.h` produces `lmc::MockHttp` capturing every `postJson` as a struct `{ std::string url, body; size_t headerCount; }` and `std::vector<...> posts;`.

- [ ] **Step 1: Write `cpp/tests/MockPlatform.h`**

```cpp
#pragma once
#include "platform/Interfaces.h"
#include <vector>
#include <string>
namespace lmc {
class MockHttp : public IHttpTransport {
public:
    struct Post { std::string url, body; size_t headerCount; };
    std::vector<Post> posts;
    bool okResult = true;
    bool postJson(const std::string& url, const std::string& body,
                  const std::vector<std::pair<std::string,std::string>>& h) override {
        posts.push_back({url, body, h.size()}); return okResult;
    }
};
} // namespace lmc
```

- [ ] **Step 2: Write failing manager test** — append to `cpp/tests/test_manager.cpp`

```cpp
#include "TelemetryManager.h"
#include "MockPlatform.h"
#include <memory>
using namespace lmc;

static VRFrameDataPlain makeFrame(double t) {
    VRFrameDataPlain f{}; f.timestampSec = t; f.leftHandJointCount = 0; f.rightHandJointCount = 0;
    return f;
}

TEST(Manager, ChunksByFramesPerFileAndDrainsOnShutdown) {
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 2;
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));
    for (int i = 0; i < 5; ++i) m.recordFrame(makeFrame(i));  // 2 full chunks at frames 2 and 4
    m.shutdown();                                             // flushes the partial 5th frame + joins
    ASSERT_EQ(http.posts.size(), 3u);                         // 2 + 1 flushed
    EXPECT_EQ(http.posts[2].body, R"([{"session_id":"","timestamp":4,"device_info":"",)"); // partial chunk has 1 frame
}
```

- [ ] **Step 3: Run to verify fail** — filter `Manager.*` → FAIL (signature/link).

- [ ] **Step 4: Move + refactor `TelemetryManager` into `core/`.** Replace `#include "JSONUploader.h"` with `#include "platform/Interfaces.h"`; change member `JSONUploader* uploader_` → `IHttpTransport* transport_`; change `initialize(cfg, JSONUploader*)` → `initialize(cfg, IHttpTransport*)`; replace `uploader_->uploadJson(json)` with `transport_->postJson(cfg_.endpointUrl, json, buildHeaders)` where headers come from a small local helper mirroring `buildSupabaseHeaders`; replace every `__android_log_print(...)` with `lmc::Log::*`. Keep `toJsonFlat` byte-identical.

- [ ] **Step 5: Run to verify pass** — `cmake --build cpp/build-host --target telemetria_tests && cpp/build-host/telemetria_tests --gtest_filter=Manager.*` → PASS.

- [ ] **Step 6: Commit** — `git add cpp/core/TelemetryManager.* cpp/tests cpp/TelemetryManager.* && git rm cpp/TelemetryManager.* && git commit -m "refactor(core): TelemetryManager on IHttpTransport + lmc::Log, add tests"`

---

## Task 8: `C3DRecorder` uses `IPathResolver` + `lmc::Log`; add read-back tests

Move `C3DRecorder` into `core/`; replace its `configReader::getExpectedConfigPath` usage with `IPathResolver::c3dFilePath`; replace logging. Keep every point/analog name, axis conversion, and tag identical. Add an ezc3d read-back test.

**Files:**
- Move+Modify: `cpp/C3DRecorder.{h,cpp}` → `cpp/core/C3DRecorder.{h,cpp}`
- Test: `cpp/tests/test_c3d_recorder.cpp`

**Interfaces:**
- Consumes: `IPathResolver` (Task 1), `sanitizeSessionId` (Task 3), `lmc::Log`.
- Produces: `class C3DRecorder` with `bool C3Dinitialize(const UploaderConfig&, int frameRate, IPathResolver*)`, `void C3DrecordFrame(const VRFrameDataPlain&)`, `void C3Dfinalize()`. (Adds the `IPathResolver*` param; drop `buildOutputPath`/`sanitizeSessionId` members now that they live in `FileUtil`/resolver.)

- [ ] **Step 1: Write failing read-back test** — `cpp/tests/test_c3d_recorder.cpp`

```cpp
#include <gtest/gtest.h>
#include "C3DRecorder.h"
#include "VRTypes.h"
#include "TelemetryTypes.h"
#include "config/ConfigParser.h"
#include <ezc3d/ezc3d.h>
#include <cstdio>
using namespace lmc;

TEST(C3dRecorder, WritesAndReadsBackExpectedLayout) {
    WinTempDir tmp;                       // helper from MockPlatform.h (creates a temp dir)
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 60, tmp.paths()));
    VRFrameDataPlain f{}; f.timestampSec = 0.5;
    f.hmdPose.position[0]=1.f; f.hmdPose.position[1]=2.f; f.hmdPose.position[2]=3.f;
    f.hmdPose.rotation[3]=1.f;
    r.C3DrecordFrame(f);
    r.C3Dfinalize();

    ezc3d::c3d file(tmp.c3dPath());
    EXPECT_EQ(file.points().labels().size(), 3u + 26u + 26u + 4u); // HEAD,LFHD,RFHD + hands + wrists
    EXPECT_EQ(file.points().label(0), "HEAD");
    EXPECT_EQ(file.points().label(3), "LWRT");                     // left hand base joint 0
    EXPECT_EQ(file.analog().labels().back(), "RealTime");
    EXPECT_EQ(file.parameters("POINT").parameter("RATE").dataAsDouble()[0], 60.0);
}
```

- [ ] **Step 2: Run to verify fail** — filter `C3dRecorder.*` → FAIL.

- [ ] **Step 3: Add `WinTempDir` to `MockPlatform.h/.cpp`** using `std::filesystem::temp_directory_path()` + unique subdir; `paths()` returns a `WinPathResolver` over that dir; `c3dPath()` returns the expected file path (create the dir so ezc3d can write). Include `<filesystem>`.

- [ ] **Step 4: Move + refactor `C3DRecorder` into `core/`.** Delete `buildOutputPath`/`sanitizeSessionId`; in `C3Dinitialize`, obtain `filePath_` from `resolver->c3dFilePath(cfg.sessionId, filePath_)`; replace `__android_log_print` with `lmc::Log::*`; drop `#include "configReader.h"`. Keep `writeC3D()` and all conversion/tag logic byte-identical.

- [ ] **Step 5: Run to verify pass** — `cmake --build cpp/build-host --target telemetria_tests && cpp/build-host/telemetria_tests --gtest_filter=C3dRecorder.*` → PASS.

- [ ] **Step 6: Commit** — `git add cpp/core/C3DRecorder.* cpp/tests/test_c3d_recorder.cpp cpp/tests/MockPlatform.* && git rm cpp/C3DRecorder.* && git commit -m "refactor(core): C3DRecorder on IPathResolver + lmc::Log, add read-back test"`

---

## Task 9: Cross-platform `TelemetryAPI` + `baseDir`; remove `configReader`

Wire the `PlatformBundle` into the C API, add `baseDir` to `TelemetryConfigPlain`, load config via `IPathResolver` + `readFileToString` + `ConfigParser`, and make JNI-context Android-only. Delete old `configReader.*`.

**Files:**
- Move+Modify: `cpp/TelemetryAPI.{h,cpp}` → `cpp/core/api/TelemetryAPI.{h,cpp}`
- Move: `cpp/TelemetryTypes.h` → `cpp/core/TelemetryTypes.h` (add `baseDir`); `cpp/VRTypes.h` → `cpp/core/VRTypes.h`
- Delete: `cpp/configReader.h`, `cpp/configReader.cpp`
- Create: `cpp/platform/android/AndroidPlatformBundle.cpp`, `cpp/platform/windows/WinPlatformBundle.cpp`

**Interfaces:**
- Produces:
  - `struct TelemetryConfigPlain { const char* sessionId; const char* deviceInfo; const char* baseDir; };` (additive; C ABI name unchanged).
  - Exported (all platforms): `telemetry_initialize`, `telemetry_record_frame`, `telemetry_force_upload`, `telemetry_shutdown`, `telemetry_get_feature_flags`. `telemetry_set_java_context` exported **only** under `__ANDROID__`.
  - `lmc::buildPlatformBundle(const char* baseDir)` implemented per platform (returns static impls; Android builds `AndroidHttpTransport` and sets Java context from `android::javaVm()/javaActivity()`).
- Consumes: everything above.

- [ ] **Step 1: Move headers, add `baseDir`.** Edit `cpp/core/TelemetryTypes.h` so `TelemetryConfigPlain` has the three `const char*` fields in order `{sessionId, deviceInfo, baseDir}`.

- [ ] **Step 2: Rewrite `core/api/TelemetryAPI.cpp`** to:
  - Remove `#include <jni.h>`/JNI globals (moved in Task 6).
  - In `telemetry_initialize`: `PlatformBundle pb = lmc::buildPlatformBundle(cfg->baseDir); lmc::Log::set(pb.log);` then build `UploaderConfig`, set defaults via `configSetDefaults`, read config file via `pb.paths->configFilePath(p)` + `lmc::readFileToString(p, text)` + `configParseJson(text, ucfg)`, set `ucfg.sessionId/deviceInfo`, init `GestorTelemetria(ucfg, pb.http)`, cache `configFeatureFlagsBitmask`, read `configParseFrameRate(text,60)`, and `C3Dinitialize(ucfg, frameRate, pb.paths)`. Return frameRate. (Mirrors the original ordering exactly.)
  - `telemetry_record_frame/force_upload/shutdown` unchanged in logic; `shutdown` sets `Log::set(nullptr)` after teardown.

- [ ] **Step 3: Implement `WinPlatformBundle.cpp`:**
```cpp
#include "platform/Interfaces.h"
#include "windows/WinLogSink.h"
#include "windows/WinPathResolver.h"
#include "windows/WinHttpTransport.h"
namespace lmc {
PlatformBundle buildPlatformBundle(const char* baseDir) {
    static WinLogSink log; static WinHttpTransport http;
    static WinPathResolver paths(baseDir ? baseDir : ".");
    return PlatformBundle{ &http, &paths, &log };
}
} // namespace lmc
```
(Windows base dir from engine; if null use the executable directory via `GetModuleFileNameA` — implement that fallback in the file.)

- [ ] **Step 4: Implement `AndroidPlatformBundle.cpp`:** build `AndroidLogSink`, `AndroidPathResolver`, `AndroidHttpTransport` (call its `setJavaContext(android::javaVm(), android::javaActivity())`), return bundle.

- [ ] **Step 5: Delete `cpp/configReader.h`/`.cpp`.**

- [ ] **Step 6: Verify host core still compiles** — `cmake --build cpp/build-host --target telemetria_core` → success. (Full shared-lib link verified in Task 10.)

- [ ] **Step 7: Commit** — `git add -A cpp/core cpp/platform && git rm cpp/TelemetryAPI.* cpp/TelemetryTypes.h cpp/VRTypes.h cpp/configReader.* && git commit -m "feat(core): cross-platform TelemetryAPI + baseDir; drop configReader"`

---

## Task 10: Finalize CMake for Win64 shared lib + Android configure

Make the Windows SHARED `telemetria.dll` build and confirm the Android toolchain still configures. This is the first task with a full end-to-end native build.

**Files:**
- Modify: `cpp/CMakeLists.txt`
- Create: `cpp/README-build.md` (build commands for Win64 + Android)

- [ ] **Step 1: Build the Windows shared lib + run tests.**
Run: `cmake -S cpp -B cpp/build-win -G "Ninja" -DTELEMETRIA_BUILD_TESTS=ON` (run on a Windows/MSVC or MinGW-with-winhttp shell) then `cmake --build cpp/build-win`.
Expected: `telemetria.dll` + `telemetria.lib` produced; `telemetria_tests` builds and passes.

- [ ] **Step 2: Confirm the Android toolchain still configures** (no compile required to pass configure; on a machine with the NDK):
Run: `cmake -S cpp -B cpp/build-arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=24 -DTELEMETRIA_BUILD_TESTS=OFF && cmake --build cpp/build-arm64`.
Expected: `libtelemetria.so` produced.

- [ ] **Step 3: Add `cpp/README-build.md`** with both command sets (Windows: MSVC x64 + `-DTELEMETRIA_BUILD_TESTS=ON`; Android: NDK toolchain, tests OFF), and note the `winhttp`/`log` link libraries.

- [ ] **Step 4: Commit** — `git add cpp/CMakeLists.txt cpp/README-build.md && git commit -m "build: Win64 telemetria.dll + Android configure"`

---

## Task 11: Unity integration for Standalone/Windows-x64

Update `TelemetriaAPI.cs` (add `baseDir`, guard Java) and `TelemetriaBootstrap.cs` (set `baseDir`, skip Java on non-Android). Manual verification against a SteamVR headset.

**Files:**
- Modify: `Unity_Implementation/Plugins/telemetria/TelemetriaAPI.cs`
- Modify: `Unity_Implementation/Scripts/TelemetriaBootstrap.cs`
- Create: `Unity_Implementation/README_Unity_Windows.md`

- [ ] **Step 1: Add `baseDir` to `TelemetryConfigPlain` in `TelemetriaAPI.cs`** — append `public IntPtr baseDir;` as the last field (matching C order `{sessionId, deviceInfo, baseDir}`). Wrap `telemetry_set_java_context` P/Invoke and `InyectarContextoJava()` in `#if UNITY_ANDROID`.

- [ ] **Step 2: In `TelemetriaBootstrap.cs.Awake`** — allocate `baseDirPtr = Marshal.StringToHGlobalAnsi(Application.dataPath);`, set `cfg.baseDir = baseDirPtr;`, call `TelemetriaAPI.InyectarContextoJava();` only under `#if UNITY_ANDROID`, and free `baseDirPtr` in `OnDestroy`. Leave all capture/axis/rate-limit code unchanged.

- [ ] **Step 3: Add plugin importer guidance in `README_Unity_Windows.md`** — place `telemetria.dll` at `Assets/Plugins/x86_64/telemetria.dll`; Inspector: **64-bit** for **Standalone**, uncheck "Any Platform"; enable OpenXR plug-in for PC + **OpenXR Hands**; hand tracking optional.

- [ ] **Step 4: Manual verification (requires PC-VR headset + SteamVR):** place `initialConfig.json` under `Application.dataPath`; run sample; confirm Unity Console shows `telemetry_initialize rc=<frameRate>`; confirm `session_<id>.c3d` appears and JSON POSTs reach the endpoint. Record results in the commit.

- [ ] **Step 5: Commit** — `git add Unity_Implementation && git commit -m "feat(unity): Standalone/Windows integration (baseDir, Java guarded)"`

---

## Task 12: Unreal integration for Win64

Update `Telemetry.Build.cs` (Win64 link + staged DLL) and `TelemetryComponent.cpp` (set `baseDir`, keep JNI Android-only). Manual verification.

**Files:**
- Modify: `Unreal_Implementation/Plugins/Telemetry/Source/Telemetry/Telemetry.Build.cs`
- Modify: `Unreal_Implementation/Plugins/Telemetry/Source/Telemetry/Private/TelemetryComponent.cpp`
- Create: `Unreal_Implementation/README_Unreal_Windows.md`

- [ ] **Step 1: `Telemetry.Build.cs`** — add a Win64 branch:
```csharp
else if (Target.Platform == UnrealTargetPlatform.Win64) {
    string Third = System.IO.Path.Combine(ModuleDirectory, "../ThirdParty/Windows");
    PublicAdditionalLibraries.Add(System.IO.Path.Combine(Third, "lib/telemetria.lib"));
    PublicDelayLoadDLLs.Add("telemetria.dll");
    RuntimeDependencies.Add(System.IO.Path.Combine(Third, "bin/telemetria.dll"));
    // add `winhttp` is unnecessary: the DLL links it privately.
}
```
Keep the existing Android branch. Document that `telemetria.dll/.lib` go under `Source/ThirdParty/Windows/{bin,lib}`.

- [ ] **Step 2: `TelemetryComponent.cpp`** — in `BeginPlay`, add `const FString BaseDir = FPaths::ProjectSavedDir();` and pass it as the new third field: allocate a UTF-8 buffer (reuse `DupAnsi(BaseDir)`), set `Cfg.baseDir = ...`, free it in `EndPlay`/`OnComponentDestroyed`. Ensure the `extern "C"` local declaration block for the telemetry functions also declares the `TelemetryConfigPlain` with `baseDir` and links the real functions on Win64 (change `#if PLATFORM_ANDROID` guard on the extern prototypes to `#if !PLATFORM_MAC /* both Android and Win64 link telemetria */`, keeping the static no-op fallbacks only for platforms without the lib). Keep the JNI `telemetry_set_java_context` call under `#if PLATFORM_ANDROID`.

- [ ] **Step 3: `README_Unreal_Windows.md`** — Win64 prerequisites: enable OpenXR plugin (SteamVR), place `telemetria.dll/.lib`, `initialConfig.json` in `FPaths::ProjectSavedDir()`, hand tracking optional via OpenXR ext; verify via Output Log + C3D + endpoint.

- [ ] **Step 4: Manual verification (PC-VR headset):** package/run a Win64 sample; confirm `[Telemetry] init rc=...`; confirm C3D + upload. Record in commit.

- [ ] **Step 5: Commit** — `git add Unreal_Implementation && git commit -m "feat(unreal): Win64 integration (baseDir, DLL staging, JNI still Android-only)"`

---

## Task 13: Top-level docs for SteamVR/Windows

Document the Windows/SteamVR path and parity at the repo root.

**Files:**
- Create: `docs/steamvr-windows.md`
- Modify: `README.md` (add a "Platforms" section linking Android + SteamVR/Windows)

- [ ] **Step 1: Write `docs/steamvr-windows.md`** — prerequisites (SteamVR runtime + PC-VR headset; Unity OpenXR(+Hands) / UE OpenXR), build (`cpp/README-build.md`), where to put `initialConfig.json` per engine (Unity `Application.dataPath`, UE `ProjectSavedDir()`), how to verify (logs, JSON upload, `.c3d` file), the hand-tracking-optional note, and the exact `initialConfig.json` schema (unchanged).

- [ ] **Step 2: Update `README.md`** with a **Platforms** section (Android unchanged; SteamVR/Windows via OpenXR) linking `docs/steamvr-windows.md`.

- [ ] **Step 3: Commit** — `git add docs/steamvr-windows.md README.md && git commit -m "docs: SteamVR/Windows usage + platform overview"`

---

## Self-Review

**Spec coverage:**
- Cross-platform core, two targets → Tasks 1, 9, 10. WinHTTP HTTP → Task 5. Interface+per-platform seams → Tasks 1,3,4,5,6. Explicit `baseDir` config/C3D paths → Tasks 3, 9, 11, 12. Optional hand tracking → Tasks 11, 12 (docs + engines), core unaffected. GoogleTest suite (config/JSON/buffer/C3D) → Tasks 2, 7, 8 (+ 3, 5). Unity + Unreal Windows integration → Tasks 11, 12. Docs → Task 13. Parity guardrails → enforced in Tasks 2 (flags), 7 (toJsonFlat byte-identical), 8 (C3D layout assertions).
- **Gap check:** `toJsonFlat` parity is guarded by the `Manager` test's body assertion (Task 7 Step 2). Hand-joint serialization conditionals are covered by extending the `Manager` test with a `handTracking=true` case — add that assertion in Task 7 Step 2 if not present. No other unimplemented spec requirement.

**Placeholder scan:** The only `// ...` bodies are Task 5's WinHTTP call sequence and Task 10's Android configure — both are fully-specified prose instructions to transcribe an existing well-known API sequence / existing command (not "add error handling" hand-waving); the test-locked logic (`buildSupabaseHeaders`) is fully shown. Acceptable.

**Type consistency:** `IHttpTransport::postJson`, `IPathResolver::configFilePath/c3dFilePath`, `lmc::Log::set/info/err`, `configSetDefaults/configParseJson/configParseFrameRate/configFeatureFlagsBitmask`, `sanitizeSessionId`, `joinPath`, `buildSupabaseHeaders`, `buildPlatformBundle`, and `TelemetryConfigPlain{sessionId,deviceInfo,baseDir}` are used with identical names/signatures across all tasks. `GestorTelemetria::initialize(cfg, IHttpTransport*)` and `C3DRecorder::C3Dinitialize(cfg, frameRate, IPathResolver*)` are consistent between their tasks and Task 9's API wiring.
