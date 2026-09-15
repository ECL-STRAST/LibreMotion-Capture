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
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/platform)
if(NOT MSVC)
    target_compile_options(telemetria_core PRIVATE -fno-exceptions -fno-rtti)
endif()
target_link_libraries(telemetria_core PUBLIC ezc3d)

# SHARED telemetry library (engine-facing C ABI).
if(TELEMETRIA_PLATFORM_SOURCES)
    add_library(telemetria SHARED ${TELEMETRIA_PLATFORM_SOURCES})
    if(NOT MSVC)
        target_compile_options(telemetria PRIVATE -fno-exceptions -fno-rtti)
    endif()
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
        tests/test_http_headers.cpp
        platform/windows/WinPathResolver.cpp
        platform/windows/WinHttpTransport.cpp)
    target_link_libraries(telemetria_tests PRIVATE telemetria_core gtest gtest_main)
    target_include_directories(telemetria_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core)
    add_test(NAME telemetria_tests COMMAND telemetria_tests)
endif()
