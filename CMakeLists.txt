cmake_minimum_required(VERSION 3.20)
project(MithrilWrapper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(MITHRIL_IOS "Build for iOS instead of macOS when APPLE is set" OFF)
set(MITHRIL_VULKAN_LIBRARY "" CACHE FILEPATH "Vulkan loader/MoltenVK library to link (iOS)")

# ---- third-party: glslang (GLSL -> SPIR-V) and SPIRV-Cross (reflection) ----
set(ENABLE_HLSL OFF CACHE BOOL "No HLSL support needed (GLES/GL only)")
set(ENABLE_OPT OFF CACHE BOOL "No spirv-opt: keep the dependency graph small")
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Skip glslangValidator CLI")
set(GLSLANG_TESTS OFF CACHE BOOL "Skip glslang unit tests")
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE BOOL "")
set(SPIRV_CROSS_FORCE_PIC ON CACHE BOOL "Build SPIRV-Cross static libs with -fPIC for inclusion in libmithril.dylib/.so")

add_subdirectory(third_party/SPIRV-Headers EXCLUDE_FROM_ALL)
add_subdirectory(third_party/SPIRV-Cross EXCLUDE_FROM_ALL)
add_subdirectory(third_party/glslang EXCLUDE_FROM_ALL)
add_subdirectory(third_party/Vulkan-Headers EXCLUDE_FROM_ALL)

add_library(mithril SHARED
    src/egl/impl.cpp
    src/egl/exports.cpp
    src/gl/exports.cpp
    src/gl/state.cpp
    src/gl/shader.cpp
    src/gl/vertex.cpp
    src/gl/draw.cpp
    src/gl/stub.cpp
    src/shader/glsl.cpp
    src/shader/reflect.cpp
    src/shader/registry.cpp
    src/state/state.cpp
    src/util/log.cpp
    src/vk/dispatch.cpp
    src/vk/target.cpp
    src/vk/pipeline.cpp
    src/vk/draw.cpp
    src/vk/engine.cpp
)

target_include_directories(mithril PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(mithril PRIVATE
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits
    spirv-cross-core
    Vulkan::Headers
)

set_target_properties(mithril PROPERTIES
    OUTPUT_NAME mithril
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output
)

if(UNIX AND NOT APPLE)
    # Linux: plain shared library for local dev loop.
elseif(APPLE AND MITHRIL_IOS)
    target_compile_definitions(mithril PUBLIC MITHRIL_IOS=1)
    target_link_libraries(mithril PUBLIC
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework QuartzCore"
        objc)
    if(MITHRIL_VULKAN_LIBRARY)
        target_link_libraries(mithril PUBLIC "${MITHRIL_VULKAN_LIBRARY}")
    endif()
elseif(APPLE)
    # macOS: export whitelist mirroring MobileGL's ExportedSymbols.txt pattern.
    target_link_options(mithril PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/scripts/exported_symbols.txt")
    set_property(TARGET mithril APPEND PROPERTY
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/exported_symbols.txt)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mithril PRIVATE -Wall -Wextra)
    # Generated stub table intentionally ignores its parameters; silence the
    # unused-parameter noise there so real warnings stay visible.
    set_source_files_properties(src/gl/exports.cpp PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter")
    # EGL placeholder surface/context routines validate the caller via EGL
    # error codes; the placeholder parameters are unused by design.
    set_source_files_properties(src/egl/exports.cpp PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter")
endif()
