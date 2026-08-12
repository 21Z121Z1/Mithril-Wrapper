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
set(MITHRIL_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/output" CACHE PATH
    "Directory for the Mithril shared-library artifact")

if(MITHRIL_IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR
        "MITHRIL_IOS=ON requires an iPhoneOS CMake toolchain "
        "(-DCMAKE_SYSTEM_NAME=iOS, normally with -DCMAKE_OSX_SYSROOT=iphoneos)")
endif()

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
    src/backend/backend.cpp
    src/egl/impl.cpp
    src/egl/exports.cpp
    src/gl/exports.cpp
    src/gl/state.cpp
    src/gl/shader.cpp
    src/gl/vertex.cpp
    src/gl/texture.cpp
    src/gl/draw.cpp
    src/gl/stub.cpp
    src/gl/fbo.cpp
    src/gl/sync.cpp
    src/gl/query.cpp
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
    src/vk/texture.cpp
    src/vk/fbo.cpp
)

target_include_directories(mithril PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_include_directories(mithril PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mithril PRIVATE
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits
    spirv-cross-core
    Vulkan::Headers
)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    # engine_apple.mm includes the shared engine implementation and replaces
    # only the CAMetalLayer presentation seam. Do not compile engine.mm as a
    # second translation unit or SetNativeWindow/Present would be duplicated.
    target_sources(mithril PRIVATE src/metal/engine_apple.mm)
    target_compile_definitions(mithril PRIVATE MITHRIL_HAS_DIRECT_METAL=1)
    set_source_files_properties(src/metal/engine_apple.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(mithril PRIVATE
        spirv-cross-msl
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore")
endif()

include(CTest)
if(APPLE AND NOT MITHRIL_IOS AND BUILD_TESTING)
    add_executable(amethyst_egl_smoke tests/amethyst_egl_smoke.mm)
    set_source_files_properties(tests/amethyst_egl_smoke.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(amethyst_egl_smoke PRIVATE
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore")
    target_compile_options(amethyst_egl_smoke PRIVATE
        -Wall -Wextra -Werror)
    add_dependencies(amethyst_egl_smoke mithril)
    add_test(NAME amethyst_egl_smoke COMMAND amethyst_egl_smoke)
    set_tests_properties(amethyst_egl_smoke PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        ENVIRONMENT
            "MITHRIL_BACKEND=metal;MITHRIL_EXPECT_RENDERER=DirectMetal;MITHRIL_LIBRARY=./output/libmithril.dylib;MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1"
        LABELS "amethyst;presentation"
        PASS_REGULAR_EXPRESSION "AMETHYST EGL SMOKE ALL PASSED")
endif()

include(cmake/MithrilSmokeTests.cmake)

set_target_properties(mithril PROPERTIES
    OUTPUT_NAME mithril
    LIBRARY_OUTPUT_DIRECTORY "${MITHRIL_OUTPUT_DIRECTORY}"
    RUNTIME_OUTPUT_DIRECTORY "${MITHRIL_OUTPUT_DIRECTORY}"
)

if(UNIX AND NOT APPLE)
    # Linux: plain shared library for local dev loop.
elseif(APPLE AND MITHRIL_IOS)
    target_compile_definitions(mithril PUBLIC MITHRIL_IOS=1)
    # Amethyst resolves the renderer from AngelAuraAmethyst.app/Frameworks via
    # @rpath/libmithril.dylib. Make the Mach-O identity match that runtime
    # contract even for an uninstalled CI build artifact.
    set_target_properties(mithril PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_NAME_DIR ON)
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
