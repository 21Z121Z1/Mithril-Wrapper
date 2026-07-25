cmake_minimum_required(VERSION 3.22.1)
project("mithril" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Objective-C++ (.mm) standard for the EGL layer (only .mm files left in the
# Vulkan/MoltenVK rewrite are egl/egl.mm and MG_Impl/Getter_gpu.mm — both pure
# Foundation/QuartzCore; no Metal framework is referenced directly).
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# GL/EGL symbols must be exported (the dylib is a drop-in OpenGL/EGL
# replacement). IMPORTANT: .mm files (EGL, Getter_gpu) are compiled as OBJCXX;
# leetal/ios-cmake defaults OBJCXX to -fvisibility=hidden, which would hide the
# egl* entry points and make host dlsym("eglCreateContext") fail. Mirror the
# CXX visibility on OBJCXX so egl.mm's extern "C" functions are exported. (The
# egl.mm translation unit also guards itself with #pragma GCC visibility
# push(default); both layers are intentional belt-and-suspenders.)
set(CMAKE_CXX_VISIBILITY_PRESET default)
set(CMAKE_C_VISIBILITY_PRESET default)
set(CMAKE_OBJCXX_VISIBILITY_PRESET default)
set(CMAKE_VISIBILITY_INLINES_HIDDEN OFF)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=default")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")
if(APPLE)
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fvisibility=default")
endif()

# ---------------------------------------------------------------------------
# MoltenVK: Vulkan 1.2 implementation backed by Metal 2.
#
# MoltenVK is consumed as a static .xcframework (MoltenVK.xcframework) plus its
# bundled Vulkan-Headers include tree. There is no Vulkan loader / ICD file —
# the static link embeds the MoltenVK ICD, so vkCreateInstance etc. resolve at
# link time. The xcframework is expected at one of:
#   - ${MOLTENVK_ROOT}                 (explicit -DMOLTENVK_ROOT=/path)
#   - ${CMAKE_SOURCE_DIR}/MoltenVK.xcframework
#   - ${CMAKE_SOURCE_DIR}/3rdparty/MoltenVK/MoltenVK.xcframework
# The CI workflow (build.yml) downloads it from the KhronosGroup/MoltenVK
# release tarball before configuring.
# ---------------------------------------------------------------------------
if(APPLE)
    # MoltenVK v1.4.x tarball ships Vulkan headers in a sibling `include/`
    # directory NEXT TO the static/dynamic xcframeworks — NOT inside the
    # xcframework itself. The CI workflow (build.yml) extracts both into:
    #   ./MoltenVK-Headers/vulkan/vulkan.h   (the headers tree)
    #   ./MoltenVK.xcframework/ios-arm64/libMoltenVK.a   (the static lib)
    # We search MoltenVK-Headers first, then fall back to the legacy
    # `MoltenVK.xcframework/Headers` layout (older tarballs / Vulkan SDK)
    # and the per-platform slice `ios-arm64/Headers` (dynamic framework).
    find_path(MOLTENVK_INCLUDE_DIR
        NAMES vulkan/vulkan.h
        PATHS
            "${MOLTENVK_ROOT}/MoltenVK-Headers"
            "${CMAKE_SOURCE_DIR}/MoltenVK-Headers"
            "${CMAKE_SOURCE_DIR}/3rdparty/MoltenVK/MoltenVK-Headers"
            "${MOLTENVK_ROOT}/MoltenVK.xcframework/Headers"
            "${CMAKE_SOURCE_DIR}/MoltenVK.xcframework/Headers"
            "${CMAKE_SOURCE_DIR}/3rdparty/MoltenVK/MoltenVK.xcframework/Headers"
            "${CMAKE_SOURCE_DIR}/Mithril-Wrapper-cpp/3rdparty/MoltenVK/MoltenVK.xcframework/Headers"
            "${CMAKE_SOURCE_DIR}/MoltenVK.xcframework/ios-arm64/Headers"
            "${CMAKE_SOURCE_DIR}/MoltenVK.xcframework/ios-arm64_x86_64-simulator/Headers"
        REQUIRED
    )
    message(STATUS "MoltenVK headers: ${MOLTENVK_INCLUDE_DIR}")

    # The static framework lives under ios-arm64 (device) or ios-arm64_x86_64-simulator
    # (simulator) inside the xcframework. CMake's link step just needs the bare
    # framework path; the linker picks the right slice from the fat binary.
    find_library(MOLTENVK_LIBRARY
        NAMES MoltenVK
        PATHS
            "${MOLTENVK_ROOT}/MoltenVK.xcframework"
            "${CMAKE_SOURCE_DIR}/MoltenVK.xcframework"
            "${CMAKE_SOURCE_DIR}/3rdparty/MoltenVK/MoltenVK.xcframework"
            "${CMAKE_SOURCE_DIR}/Mithril-Wrapper-cpp/3rdparty/MoltenVK/MoltenVK.xcframework"
        PATH_SUFFIXES
            "ios-arm64"
            "ios-arm64_x86_64-simulator"
        REQUIRED
    )
    message(STATUS "MoltenVK library: ${MOLTENVK_LIBRARY}")
endif()

# ---------------------------------------------------------------------------
# glslang: GLSL front-end -> SPIR-V  (submodule under 3rdparty/, mirrors the
# MobileGlues layout which uses add_subdirectory(3rdparty/glslang) instead of
# FetchContent so the sources are available for local hacking).
#
# SPIRV-Cross is used at runtime for SPIR-V *reflection* (not translation):
# DescriptorSet.cpp reflects each linked program's VS+FS SPIR-V to discover
# UBO / sampled-image bindings, then builds VkDescriptorSetLayout /
# VkPipelineLayout / VkDescriptorPool. MoltenVK still cross-translates the
# Vulkan SPIR-V to MSL internally at vkCreateShaderModule time, so SPIRV-Cross
# is NOT used for shader translation — only for the binding reflection needed
# to wire uniforms + textures into the shader.
# ---------------------------------------------------------------------------
set(ENABLE_RTTI                ON  CACHE BOOL "" FORCE)
set(ENABLE_HLSL                OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT                 OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES    OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL             OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL       ON  CACHE BOOL "" FORCE)
set(GLSLANG_TESTS              OFF CACHE BOOL "" FORCE)
add_subdirectory(Mithril-Wrapper-cpp/3rdparty/glslang)

# SPIRV-Cross: used for SPIR-V reflection (spirv_cross::Compiler +
# get_shader_resources) in MG_Backend/DirectVulkan/DescriptorSet.cpp. Only the
# core library is needed (no GLSL/HLSL/MSL/CPP backends, no CLI, no tests).
# SPIRV-Headers (sibling submodule) provides <spirv.h> for the spv:: enums.
set(SPIRV_CROSS_STATIC                 ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI                    OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS           OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL            OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL            OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL             OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP             OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT         OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API           OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL           ON  CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/Mithril-Wrapper-cpp/3rdparty/SPIRV-Headers/include"
    CACHE PATH "" FORCE)
add_subdirectory(Mithril-Wrapper-cpp/3rdparty/SPIRV-Cross)

# ---------------------------------------------------------------------------
# Mithril-Wrapper shared library (iOS arm64 dylib)
#
# Source layout (Vulkan/MoltenVK rewrite):
#   Mithril-Wrapper-cpp/
#     MG_Impl/         — OpenGL 3.3 Core Profile entry points (GL calls into
#                        the Vulkan backend via the C API in MG_Backend/Backend.h)
#     MG_State/        — GL state machine (per-EGLContext GLState)
#     MG_Backend/      — abstract backend C API (Backend.h)
#       DirectVulkan/  — Vulkan 1.2 + MoltenVK implementation
#     egl/             — EGL 1.5 (Vulkan-backed; egl.mm is the only .mm here
#                        that touches CAMetalLayer via VK_EXT_metal_surface)
#     include/         — public GL/EGL headers
# ---------------------------------------------------------------------------
set(MITHRIL_SOURCES
    # MG_Impl: OpenGL 3.3 Core Profile entry points
    Mithril-Wrapper-cpp/MG_Impl/init.cpp
    Mithril-Wrapper-cpp/MG_Impl/Log.cpp
    Mithril-Wrapper-cpp/MG_Impl/gl.cpp
    Mithril-Wrapper-cpp/MG_Impl/Getter.cpp
    Mithril-Wrapper-cpp/MG_Impl/Program.cpp
    Mithril-Wrapper-cpp/MG_Impl/Shader.cpp
    Mithril-Wrapper-cpp/MG_Impl/Buffer.cpp
    Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp
    Mithril-Wrapper-cpp/MG_Impl/Texture.cpp
    Mithril-Wrapper-cpp/MG_Impl/Framebuffer.cpp
    Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp
    Mithril-Wrapper-cpp/MG_Impl/Stubs.cpp
    Mithril-Wrapper-cpp/MG_Impl/Debug.cpp
    Mithril-Wrapper-cpp/MG_Impl/lookup.cpp

    # MG_State: GL state machine
    Mithril-Wrapper-cpp/MG_State/State.cpp

    # MG_Backend: Vulkan 1.2 + MoltenVK direct backend
    Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Device.cpp
    Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Resources.cpp
    Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.cpp
    Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp
    Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp
    Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/ImageOps.cpp
)

if(APPLE)
    list(APPEND MITHRIL_SOURCES
        # EGL 1.5 surface backed by Vulkan 1.2 (CAMetalLayer + VK_EXT_metal_surface).
        # On iOS this is the layer Amethyst-iOS' gl_bridge.m dlsym's eglCreateContext
        # etc. against. Compiled as Objective-C++ so it can talk to CAMetalLayer.
        Mithril-Wrapper-cpp/egl/egl.mm
        # GPU info string builder (queries VkPhysicalDeviceProperties for
        # GL_RENDERER; .mm so it can use NSString/NSLog without an #ifdef dance).
        Mithril-Wrapper-cpp/MG_Impl/Getter_gpu.mm
        # Vulkan swapchain (VkSurfaceKHR via VK_EXT_metal_surface). Compiled as
        # Objective-C++ because it #defines VK_USE_PLATFORM_METAL_EXT before
        # including <vulkan/vulkan.h>, which pulls in <Metal/Metal.h> (ObjC-only)
        # for the VkMetalSurfaceCreateInfoEXT / PFN_vkCreateMetalSurfaceEXT
        # declarations. The other DirectVulkan .cpp files stay plain C++ —
        # Device.h stores the function pointer as PFN_vkVoidFunction to avoid
        # needing the metal platform define there.
        Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Swapchain.mm
    )
endif()

add_library(${PROJECT_NAME} SHARED ${MITHRIL_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Mithril-Wrapper-cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Mithril-Wrapper-cpp/include
)

# glslang (GLSL -> SPIR-V). NOTE: glslang-default-resource-limits provides
# GetDefaultResources() which lives in ResourceLimits/ResourceLimits.cpp.
# Without it the link fails with "Undefined symbols: GetDefaultResources()"
# when ENABLE_GLSLANG_BINARIES=OFF (the standalone glslangValidator that used
# to host it is disabled).
#
# spirv-cross-core: SPIR-V reflection API (spirv_cross::Compiler,
# get_shader_resources) used by DescriptorSet.cpp to build descriptor set
# layouts. The SPIRV-Headers include dir supplies <spirv.h> (spv::Decoration*).
target_link_libraries(${PROJECT_NAME} PRIVATE
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits
    spirv-cross-core
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Mithril-Wrapper-cpp/3rdparty/SPIRV-Cross
    ${CMAKE_CURRENT_SOURCE_DIR}/Mithril-Wrapper-cpp/3rdparty/SPIRV-Headers/include
)

if(APPLE)
    # MoltenVK static framework + Vulkan headers. The framework transitively
    # links Metal.framework / QuartzCore.framework / Foundation.framework, but
    # we still list them explicitly to satisfy -framework lookup when the
    # static link strips MoltenVK's lazy references.
    #
    # -ObjC: forces the linker to load all Objective-C class/category symbols
    # from static archives. MoltenVK's libMoltenVK.a contains ObjC++ code
    # (Metal interop); without this flag, categories may be stripped and cause
    # runtime "unrecognized selector" crashes. Standard Apple recommendation
    # for static libraries containing Objective-C.
    target_include_directories(${PROJECT_NAME} PRIVATE ${MOLTENVK_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${MOLTENVK_LIBRARY}
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework IOSurface"
    )
    target_link_options(${PROJECT_NAME} PRIVATE -ObjC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MITHRIL_VULKAN=1)
    # VK_ENABLE_BETA_EXTENSIONS: vulkan_core.h only includes
    # vulkan_portability_subset.h (which defines
    # VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME and the portability-subset
    # feature/property structs) when this macro is defined. MoltenVK always
    # advertises VK_KHR_portability_subset, and Device.cpp enables it by name,
    # so the macro MUST be visible across the whole build. (Safe to define
    # globally — it only gates beta-extension declarations, no codegen effect.)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_ENABLE_BETA_EXTENSIONS=1)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
endif()

# ARC for Objective-C++ translation units (EGL, GPU info, Vulkan swapchain).
if(APPLE)
    set_source_files_properties(
        Mithril-Wrapper-cpp/egl/egl.mm
        Mithril-Wrapper-cpp/MG_Impl/Getter_gpu.mm
        Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Swapchain.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
endif()

# Reasonable warnings, no -Werror (CI must be tolerant during bring-up)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "mithril"
    PREFIX "lib"
    SUFFIX ".dylib"
)
