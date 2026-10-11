# Nova Engine — root CMakeLists.txt
#
# Drives both build paths:
#  * Android (via AGP externalNativeBuild): produces libnova.so.
#  * Host (CI / developer machines): builds the engine core, unit tests, and
#    — with -DNOVA_HOST_CHECK=ON — compile-checks the Android-only sources
#    (JNI bridge, ANativeWindow/AAsset paths, Vulkan backend) against the
#    shim headers in tools/host_check/.
cmake_minimum_required(VERSION 3.22)

project(NovaEngine VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options (feature flags per coding standards; subsystems join in later
# milestones — enabling one whose subsystem is not merged yet prints a notice).
# ---------------------------------------------------------------------------
option(NOVA_BUILD_TESTS "Build unit tests (host builds only)" ON)
option(NOVA_HOST_CHECK "Compile-check Android-only sources on this host" OFF)
option(NOVA_ENABLE_RHI_VULKAN "Compile the Vulkan 1.3 RHI backend" ON)
option(NOVA_ENABLE_RHI_GLES "Compile the OpenGL ES 3.2 RHI backend" OFF)
option(NOVA_ENABLE_PHYSICS "Physics module (Jolt)" OFF)
option(NOVA_ENABLE_AUDIO "Audio module (Oboe/miniaudio)" OFF)
option(NOVA_ENABLE_LUA "Lua scripting (Sol2)" OFF)
option(NOVA_ENABLE_NET "Networking module (ENet)" OFF)
option(NOVA_ENABLE_UI "RmlUi in-game UI" OFF)
option(NOVA_ENABLE_ANIMATION "Animation module (ozz)" OFF)
option(NOVA_ENABLE_PROFILER "Tracy profiler integration" OFF)
option(NOVA_ENABLE_SANITIZERS "Build with ASan/UBSan" OFF)
option(NOVA_ENABLE_LTO "Interprocedural optimization for release builds" ON)
set(NOVA_JNI_INCLUDE "" CACHE PATH "Path to jni.h include dir (required by NOVA_HOST_CHECK)")

# Subsystems arriving with later milestones — honest status, not silent no-ops.
foreach(module physics audio lua net ui animation profiler)
    string(TOUPPER "${module}" module_upper)
    if(NOVA_ENABLE_${module_upper})
        message(STATUS "nova: NOVA_ENABLE_${module_upper}=ON, but the ${module} "
                       "subsystem ships with a later milestone — nothing to build yet")
    endif()
endforeach()
if(NOVA_ENABLE_RHI_GLES)
    message(STATUS "nova: NOVA_ENABLE_RHI_GLES=ON, but the GLES backend ships after M1 — "
                   "nothing to build yet")
endif()

# ---------------------------------------------------------------------------
# Toolchain / global flags
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOVA_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT nova_ipo_ok OUTPUT nova_ipo_msg)
    if(nova_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(STATUS "nova: LTO unavailable with this toolchain (${nova_ipo_msg})")
    endif()
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Engine library + per-module wiring
# ---------------------------------------------------------------------------
add_subdirectory(engine)

# Milestone modules register themselves here as they land (M2 scene/ecs,
# M3 assets, M5 physics, ...).

# ---------------------------------------------------------------------------
# Android: the shared library AGP packages as libnova.so
# ---------------------------------------------------------------------------
if(ANDROID)
    add_library(nova SHARED app/src/main/cpp/jni_bridge.cpp)
    target_link_libraries(nova PRIVATE nova_engine Threads::Threads)

    # JNI glue keeps exceptions (engine core is -fno-exceptions).
    target_compile_options(nova PRIVATE -fvisibility=hidden)

    find_library(nova_log_lib log REQUIRED)
    find_library(nova_android_lib android REQUIRED)
    find_library(nova_vulkan_lib vulkan REQUIRED)
    target_link_libraries(nova PRIVATE
        ${nova_log_lib}
        ${nova_android_lib}
        ${nova_vulkan_lib}
    )

    if(NOVA_ENABLE_SANITIZERS)
        target_compile_options(nova PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(nova PRIVATE -fsanitize=address)
    endif()

# ---------------------------------------------------------------------------
# Host: compile-check the JNI bridge + Android surfaces against shims
# ---------------------------------------------------------------------------
elseif(NOVA_HOST_CHECK)
    if(NOT NOVA_JNI_INCLUDE)
        if(DEFINED ENV{JAVA_HOME} AND EXISTS "$ENV{JAVA_HOME}/include/jni.h")
            set(NOVA_JNI_INCLUDE "$ENV{JAVA_HOME}/include" CACHE PATH "" FORCE)
        else()
            message(FATAL_ERROR
                "NOVA_HOST_CHECK needs jni.h: pass -DNOVA_JNI_INCLUDE=/path/to/jdk/include")
        endif()
    endif()

    add_library(nova_jni_check OBJECT app/src/main/cpp/jni_bridge.cpp)
    target_include_directories(nova_jni_check PRIVATE "${NOVA_JNI_INCLUDE}")
    target_link_libraries(nova_jni_check PRIVATE nova_engine Threads::Threads)
    # Mirrors the Android build: exceptions allowed in the JNI TU.
    message(STATUS "nova: host compile-check of jni_bridge.cpp enabled")
endif()

# ---------------------------------------------------------------------------
# Tests (host only)
# ---------------------------------------------------------------------------
if(NOVA_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_subdirectory(engine/tests)
endif()
