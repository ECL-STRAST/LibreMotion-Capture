cmake_minimum_required(VERSION 3.22)
project(telemetria LANGUAGES C CXX)

option(TELEMETRIA_BUILD_TESTS "Build GoogleTest unit tests (host only)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ezc3d: always static, never its tests or its examples. These are the option names
# the vendored version actually defines; the EZC3D_* spellings used here previously
# matched nothing and silently left the examples building in every configuration.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLE     OFF CACHE BOOL "" FORCE)
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
    # loses all 6 portable entry points). Fix (chosen over a .def file, which would
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
    # Pick a symbol dump tool that actually exists: MSVC has no objdump (CMAKE_OBJDUMP
    # is empty -> the first symbol would be consumed as the tool argument) but ships
    # dumpbin; MinGW/NDK have objdump. The gate is a POSIX shell script, and cmd.exe
    # cannot execute a .sh, so a shell is required too. When the tool or the shell is
    # missing (bare Windows shell) the step is SKIPPED with a status message: a missing
    # analysis tool must never hard-fail an otherwise-good build.
    find_program(TELEMETRIA_DUMPBIN NAMES dumpbin)
    find_program(TELEMETRIA_POSIX_SHELL NAMES sh bash)
    set(TELEMETRIA_EXPORT_TOOL "")
    if(TELEMETRIA_DUMPBIN)
        set(TELEMETRIA_EXPORT_TOOL "${TELEMETRIA_DUMPBIN}")
    elseif(CMAKE_OBJDUMP AND EXISTS "${CMAKE_OBJDUMP}")
        set(TELEMETRIA_EXPORT_TOOL "${CMAKE_OBJDUMP}")
    endif()
    if(TELEMETRIA_EXPORT_TOOL AND TELEMETRIA_POSIX_SHELL)
        add_custom_command(TARGET telemetria POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "checking exported C ABI..."
            COMMAND "${TELEMETRIA_POSIX_SHELL}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_exports.sh"
                    $<TARGET_FILE:telemetria> "${TELEMETRIA_EXPORT_TOOL}"
                    telemetry_initialize telemetry_record_frame telemetry_force_upload
                    telemetry_shutdown telemetry_get_feature_flags telemetry_set_frame_rate
                    telemetry_record_frame_ex
            COMMENT "Verifying telemetria C ABI exports"
            VERBATIM)
        if(ANDROID)
            # JNI extras that must survive linking on Android.
            add_custom_command(TARGET telemetria POST_BUILD
                COMMAND "${TELEMETRIA_POSIX_SHELL}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_exports.sh"
                        $<TARGET_FILE:telemetria> "${TELEMETRIA_EXPORT_TOOL}"
                        telemetry_set_java_context JNI_OnLoad
                COMMENT "Verifying telemetria JNI exports"
                VERBATIM)
        endif()
    else()
        message(STATUS "telemetria: no dumpbin/objdump (or no POSIX shell) found, "
                       "skipping the C ABI export check")
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
        tests/test_c3d_validity.cpp
        tests/test_http_headers.cpp
        tests/test_winhttp_target.cpp
        tests/test_api_frame_rate.cpp
        platform/windows/WinPathResolver.cpp
        platform/windows/WinHttpTransport.cpp)
    target_link_libraries(telemetria_tests PRIVATE telemetria_core gtest gtest_main)
    target_include_directories(telemetria_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core)
    add_test(NAME telemetria_tests COMMAND telemetria_tests)
endif()
