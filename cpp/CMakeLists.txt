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
    core/HttpHeaders.cpp
    core/C3DRecorder.cpp
    core/config/ConfigParser.cpp
    core/platform/Log.cpp
    core/platform/FileUtil.cpp)

# STATIC core used by both the shared lib and the tests.
add_library(telemetria_core STATIC ${TELEMETRIA_CORE_SOURCES})
target_include_directories(telemetria_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/platform)
if(NOT MSVC)
    target_compile_options(telemetria_core PRIVATE -fno-exceptions -fno-rtti)
endif()
# On Windows the API entry points must be *definitions* (dllexport), not dllimport,
# whenever this archive is compiled: a dllimport declaration plus a local definition is
# an error on MSVC (C2491) and a -Wattributes warning on MinGW, and any consumer that
# includes TelemetryAPI.h while linking the static archive would otherwise look for the
# nonexistent __imp_* thunks (verified: undefined reference to `__imp_telemetry_initialize`).
# PUBLIC so static consumers (tests) see the same linkage as the archive provides.
if(WIN32)
    target_compile_definitions(telemetria_core PUBLIC TELEMETRIA_EXPORTS)
endif()
target_link_libraries(telemetria_core PUBLIC ezc3d)

# SHARED telemetry library (engine-facing C ABI).
if(TELEMETRIA_PLATFORM_SOURCES)
    # The C ABI entry points live in core/api/TelemetryAPI.cpp, which otherwise only
    # reaches telemetria_core (STATIC). A static archive member is pulled in only if
    # some symbol is still undefined, and nothing in the platform sources references
    # it -> the object is dropped and the shared lib exports nothing usable
    # (Windows: Unreal fails with LNK2019 / delay-load; Android: the .so silently
    # loses all 5 portable entry points). Fix (chosen over a .def file, which would
    # need per-toolchain link flags and still compile the core copy as dllimport):
    # always compile the API translation unit into the shared target. On Windows
    # TELEMETRIA_EXPORTS turns them into __declspec(dllexport) so MSVC also emits the
    # telemetria.lib import lib Unreal links; elsewhere default visibility applies.
    # The core archive keeps the same TU for static consumers (host tests).
    add_library(telemetria SHARED ${TELEMETRIA_PLATFORM_SOURCES} core/api/TelemetryAPI.cpp)
    if(NOT MSVC)
        target_compile_options(telemetria PRIVATE -fno-exceptions -fno-rtti)
    endif()
    if(WIN32)
        target_compile_definitions(telemetria PRIVATE TELEMETRIA_EXPORTS)
    endif()
    target_link_libraries(telemetria PRIVATE telemetria_core)
    if(ANDROID)
        target_link_libraries(telemetria PRIVATE log)
    elseif(WIN32)
        target_link_libraries(telemetria PRIVATE winhttp)
    endif()
    # Cheap guard: fail the build if the C ABI disappears from the artifact again.
    add_custom_command(TARGET telemetria POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "checking exported C ABI..."
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_exports.sh
                $<TARGET_FILE:telemetria> ${CMAKE_OBJDUMP}
                telemetry_initialize telemetry_record_frame telemetry_force_upload
                telemetry_shutdown telemetry_get_feature_flags
        COMMENT "Verifying telemetria C ABI exports"
        VERBATIM)
    if(ANDROID)
        # JNI extras that must survive linking on Android.
        add_custom_command(TARGET telemetria POST_BUILD
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_exports.sh
                    $<TARGET_FILE:telemetria> ${CMAKE_OBJDUMP}
                    telemetry_set_java_context JNI_OnLoad
            COMMENT "Verifying telemetria JNI exports"
            VERBATIM)
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
        tests/test_http_headers.cpp
        platform/windows/WinPathResolver.cpp
        platform/windows/WinHttpTransport.cpp)
    target_link_libraries(telemetria_tests PRIVATE telemetria_core gtest gtest_main)
    target_include_directories(telemetria_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core)
    add_test(NAME telemetria_tests COMMAND telemetria_tests)
endif()
