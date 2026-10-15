# Nova Engine — core engine static library.
#
# Source lists are explicit per coding standards; Vulkan sources join the
# build only for Android or NOVA_HOST_CHECK (a host link cannot resolve
# libvulkan, so plain host builds compile the backend-neutral subset and run
# tests against it).

set(NOVA_CORE_SOURCES
    core/Log.cpp
    core/Time.cpp
    core/Memory.cpp
    core/Console.cpp
)

set(NOVA_PLATFORM_SOURCES
    platform/InputQueue.cpp
    platform/VirtualFileSystem.cpp
)

set(NOVA_RHI_SOURCES
    rhi/RhiTypes.cpp
    rhi/DeviceFactory.cpp
)

set(NOVA_RENDERER_SOURCES
    renderer/DrawList.cpp
    renderer/Renderer.cpp
    renderer/BuiltinGeometry.cpp
)

set(NOVA_ASSETS_SOURCES
    assets/Base64.cpp
    assets/Json.cpp
)

set(NOVA_SCENE_SOURCES
    scene/World.cpp
    scene/SceneHierarchy.cpp
    scene/Frustum.cpp
    scene/Culling.cpp
)

set(NOVA_ENGINE_SOURCES
    Engine.cpp
)

set(NOVA_VULKAN_COMPILED OFF)
if(NOVA_ENABLE_RHI_VULKAN AND (ANDROID OR NOVA_HOST_CHECK))
    list(APPEND NOVA_RHI_SOURCES
        rhi/vulkan/VulkanDevice.cpp
        rhi/vulkan/VulkanResources.cpp
        rhi/vulkan/VmaImplementation.cpp
    )
    set(NOVA_VULKAN_COMPILED ON)
endif()

add_library(nova_engine STATIC
    ${NOVA_CORE_SOURCES}
    ${NOVA_PLATFORM_SOURCES}
    ${NOVA_RHI_SOURCES}
    ${NOVA_RENDERER_SOURCES}
    ${NOVA_SCENE_SOURCES}
    ${NOVA_ASSETS_SOURCES}
    ${NOVA_ENGINE_SOURCES}
    editor_core/SceneDocument.cpp
)

# Public includes: engine modules are referenced as "core/Log.h", "rhi/...".
target_include_directories(nova_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Third-party header roots.
target_include_directories(nova_engine SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glm
)
target_include_directories(nova_engine SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vulkan-headers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vk_mem_alloc
)

target_compile_features(nova_engine PUBLIC cxx_std_20)
target_link_libraries(nova_engine PUBLIC Threads::Threads)

# Engine runtime is -fno-exceptions (Result<T,E> instead) with hidden
# visibility; warnings on for first-party code only.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nova_engine PRIVATE
        -fno-exceptions
        -fvisibility=hidden
        -Wall
        -Wextra
        -Wshadow
    )
endif()

if(NOVA_VULKAN_COMPILED)
    target_compile_definitions(nova_engine PUBLIC NOVA_VULKAN_COMPILED=1)
endif()

if(NOVA_HOST_CHECK AND NOT ANDROID)
    target_compile_definitions(nova_engine PUBLIC NOVA_HOST_ANDROID=1)
    target_include_directories(nova_engine SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../tools/host_check/include
    )
    if(NOVA_JNI_INCLUDE)
        target_include_directories(nova_engine SYSTEM PUBLIC "${NOVA_JNI_INCLUDE}")
        # JDKs keep jni_md.h in a per-OS subdirectory; the NDK keeps it next
        # to jni.h. Support both layouts.
        if(EXISTS "${NOVA_JNI_INCLUDE}/jni_md.h")
            # nothing extra
        elseif(EXISTS "${NOVA_JNI_INCLUDE}/linux/jni_md.h")
            target_include_directories(nova_engine SYSTEM PUBLIC
                "${NOVA_JNI_INCLUDE}/linux")
        elseif(EXISTS "${NOVA_JNI_INCLUDE}/darwin/jni_md.h")
            target_include_directories(nova_engine SYSTEM PUBLIC
                "${NOVA_JNI_INCLUDE}/darwin")
        endif()
    endif()
    # Functional no-op implementations so host links of the Android paths
    # succeed (see tools/host_check/README.md).
    target_sources(nova_engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tools/host_check/shim_impl.cpp
    )
endif()

if(NOVA_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nova_engine PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(nova_engine PUBLIC -fsanitize=address)
endif()

if(ANDROID AND NOVA_ENABLE_RHI_GLES)
    target_sources(nova_engine PRIVATE rhi/gles/GlesDevice.cpp)
    target_compile_definitions(nova_engine PUBLIC NOVA_GLES_COMPILED=1)
    find_library(nova_egl_lib EGL REQUIRED)
    find_library(nova_gles_lib GLESv2 REQUIRED)
    target_link_libraries(nova_engine INTERFACE ${nova_egl_lib} ${nova_gles_lib})
endif()

if(ANDROID AND NOVA_ENABLE_RHI_VULKAN)
    # Carried to libnova.so through the link interface.
    find_library(nova_engine_vulkan_lib vulkan REQUIRED)
    target_link_libraries(nova_engine INTERFACE ${nova_engine_vulkan_lib})
endif()
