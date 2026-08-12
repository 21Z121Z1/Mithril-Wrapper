cmake_minimum_required(VERSION 3.20)
project(MithrilWrapper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(APPLE)
    set(_mithril_default_direct ON)
    set(_mithril_default_legacy OFF)
else()
    set(_mithril_default_direct OFF)
    set(_mithril_default_legacy ON)
endif()

option(MITHRIL_BUILD_DIRECT "Build the Vulkan-free DirectMetal shipping target" ${_mithril_default_direct})
option(MITHRIL_BUILD_LEGACY "Build the Vulkan reference target" ${_mithril_default_legacy})
option(MITHRIL_ENABLE_SHADER_TOOLCHAIN "Build GLSL/SPIR-V shader translation support" ON)
option(MITHRIL_IOS "Compatibility switch for iPhoneOS builds" OFF)
set(MITHRIL_VULKAN_LIBRARY "" CACHE FILEPATH "Vulkan loader/MoltenVK library to link for the legacy iOS target")
set(MITHRIL_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/output" CACHE PATH
    "Directory for Mithril shared-library artifacts")

if(MITHRIL_IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR
        "MITHRIL_IOS=ON requires an iPhoneOS CMake toolchain "
        "(-DCMAKE_SYSTEM_NAME=iOS and -DCMAKE_OSX_SYSROOT=iphoneos)")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MITHRIL_IOS ON CACHE BOOL "Compatibility switch for iPhoneOS builds" FORCE)
endif()
if(MITHRIL_BUILD_DIRECT AND NOT APPLE)
    message(FATAL_ERROR "MITHRIL_BUILD_DIRECT=ON is supported only on Apple platforms")
endif()
if((MITHRIL_BUILD_DIRECT OR MITHRIL_BUILD_LEGACY) AND NOT MITHRIL_ENABLE_SHADER_TOOLCHAIN)
    message(FATAL_ERROR "The current GL compatibility implementation requires MITHRIL_ENABLE_SHADER_TOOLCHAIN=ON")
endif()

# Shared shader dependencies. SPIRV-Headers is the Khronos shader IR header set;
# it is not the Vulkan-Headers package and is required by glslang in both lanes.
if(MITHRIL_BUILD_DIRECT OR MITHRIL_BUILD_LEGACY)
    set(ENABLE_HLSL OFF CACHE BOOL "No HLSL support needed")
    set(ENABLE_OPT OFF CACHE BOOL "Keep the dependency graph small")
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Skip glslangValidator CLI")
    set(GLSLANG_TESTS OFF CACHE BOOL "Skip glslang unit tests")
    set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE BOOL "")
    set(SPIRV_CROSS_FORCE_PIC ON CACHE BOOL "Build SPIRV-Cross static libs with -fPIC")

    add_subdirectory(third_party/SPIRV-Headers EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/SPIRV-Cross EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/glslang EXCLUDE_FROM_ALL)
endif()

# Vulkan-Headers exists only in the legacy build graph. A DirectMetal-only
# configure never adds the package and therefore cannot accidentally inherit
# Vulkan headers or loader targets through CMake.
if(MITHRIL_BUILD_LEGACY)
    add_subdirectory(third_party/Vulkan-Headers EXCLUDE_FROM_ALL)
endif()

set(MITHRIL_FRONTEND_SOURCES
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
)

set(MITHRIL_VULKAN_SOURCES
    src/vk/dispatch.cpp
    src/vk/target.cpp
    src/vk/pipeline.cpp
    src/vk/draw.cpp
    src/vk/engine.cpp
    src/vk/texture.cpp
    src/vk/fbo.cpp
)

function(mithril_configure_frontend_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_include_directories(${target} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${target} PRIVATE
        glslang::glslang
        glslang::SPIRV
        glslang::glslang-default-resource-limits
        spirv-cross-core)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        set_source_files_properties(src/gl/exports.cpp PROPERTIES
            COMPILE_OPTIONS "-Wno-unused-parameter")
        set_source_files_properties(src/egl/exports.cpp PROPERTIES
            COMPILE_OPTIONS "-Wno-unused-parameter")
    endif()
endfunction()

# Production Apple artifact. This target deliberately contains the mature GL
# compatibility frontend unchanged, but only the DirectMetal backend. Its
# compile definition also removes every vk::* dispatch arm from backend.cpp.
if(APPLE AND MITHRIL_BUILD_DIRECT)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    add_library(mithril_direct SHARED
        ${MITHRIL_FRONTEND_SOURCES}
        src/metal/MetalDeviceSession.cpp
        src/metal/engine_apple.mm)
    mithril_configure_frontend_target(mithril_direct)
    target_compile_definitions(mithril_direct PRIVATE
        MITHRIL_HAS_DIRECT_METAL=1
        MITHRIL_DIRECT_ONLY=1)
    target_link_libraries(mithril_direct PRIVATE
        spirv-cross-msl
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore")
    set_source_files_properties(src/metal/engine_apple.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(mithril_direct PROPERTIES
        OUTPUT_NAME mithril
        LIBRARY_OUTPUT_DIRECTORY "${MITHRIL_OUTPUT_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY "${MITHRIL_OUTPUT_DIRECTORY}")

    # Both macOS and iPhoneOS ship the same narrow C ABI. The target-level
    # visibility override below is needed on ios-cmake to make our entry-point
    # objects exportable; the ld64 whitelist then removes C++/third-party
    # internals from the final Mach-O dynamic symbol table.
    target_link_options(mithril_direct PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/scripts/exported_symbols.txt")
    set_property(TARGET mithril_direct APPEND PROPERTY
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/exported_symbols.txt)

    if(MITHRIL_IOS)
        target_compile_options(mithril_direct PRIVATE -fvisibility=default)
        set_target_properties(mithril_direct PROPERTIES
            C_VISIBILITY_PRESET default
            CXX_VISIBILITY_PRESET default
            VISIBILITY_INLINES_HIDDEN OFF)
        target_compile_definitions(mithril_direct PUBLIC MITHRIL_IOS=1)
        target_link_libraries(mithril_direct PRIVATE
            "-framework CoreGraphics"
            objc)
        set_target_properties(mithril_direct PROPERTIES
            INSTALL_NAME_DIR "@rpath"
            BUILD_WITH_INSTALL_NAME_DIR ON)
    endif()

    get_target_property(_mithril_direct_sources mithril_direct SOURCES)
    get_target_property(_mithril_direct_links mithril_direct LINK_LIBRARIES)
    foreach(_item IN LISTS _mithril_direct_sources _mithril_direct_links)
        string(TOLOWER "${_item}" _item_lower)
        if(_item_lower MATCHES "(^|[/;:])vk([/;:]|$)|vulkan|moltenvk|directvulkan|vulkan::headers")
            message(FATAL_ERROR "mithril_direct boundary violation: ${_item}")
        endif()
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mithril_direct.boundary.json"
        CONTENT "{\n  \"target\": \"mithril_direct\",\n  \"direct_only\": true,\n  \"sources\": \"$<JOIN:$<TARGET_PROPERTY:mithril_direct,SOURCES>,;>\",\n  \"links\": \"$<JOIN:$<TARGET_PROPERTY:mithril_direct,LINK_LIBRARIES>,;>\"\n}\n")
endif()

# Reference backend remains available for Linux regression and explicit legacy
# builds, but it is a distinct target and never contributes objects to
# mithril_direct. On Apple it receives a distinct output name to avoid being
# mistaken for the shipping DirectMetal dylib.
if(MITHRIL_BUILD_LEGACY)
    add_library(mithril_legacy SHARED
        ${MITHRIL_FRONTEND_SOURCES}
        ${MITHRIL_VULKAN_SOURCES})
    mithril_configure_frontend_target(mithril_legacy)
    target_link_libraries(mithril_legacy PRIVATE Vulkan::Headers)
    if(APPLE)
        set_target_properties(mithril_legacy PROPERTIES OUTPUT_NAME mithril-vulkan)
        if(MITHRIL_IOS AND MITHRIL_VULKAN_LIBRARY)
            target_link_libraries(mithril_legacy PRIVATE "${MITHRIL_VULKAN_LIBRARY}")
        endif()
    else()
        set_target_properties(mithril_legacy PROPERTIES OUTPUT_NAME mithril)
    endif()
    set_target_properties(mithril_legacy PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${MITHRIL_OUTPUT_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY "${MITHRIL_OUTPUT_DIRECTORY}")
endif()

include(CTest)
include(cmake/MithrilSmokeTests.cmake)

# Presentation-specific integration test stays separate from the generic GL
# smoke corpus because it reaches the real CAMetalLayer drawable seam.
if(APPLE AND NOT MITHRIL_IOS AND BUILD_TESTING AND TARGET mithril_direct)
    target_compile_definitions(mithril_direct PRIVATE
        MITHRIL_PRESENTATION_TEST_HOOKS=1)
    add_executable(amethyst_egl_smoke tests/amethyst_egl_smoke.mm)
    set_source_files_properties(tests/amethyst_egl_smoke.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(amethyst_egl_smoke PRIVATE
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore")
    target_compile_options(amethyst_egl_smoke PRIVATE -Wall -Wextra -Werror)
    add_dependencies(amethyst_egl_smoke mithril_direct)
    add_test(NAME amethyst_egl_smoke COMMAND amethyst_egl_smoke)
    set_tests_properties(amethyst_egl_smoke PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        ENVIRONMENT
            "MITHRIL_BACKEND=metal;MITHRIL_EXPECT_RENDERER=DirectMetal;MITHRIL_LIBRARY=./output/libmithril.dylib;MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1"
        LABELS "directmetal;amethyst;presentation"
        TIMEOUT 120
        PASS_REGULAR_EXPRESSION "AMETHYST EGL SMOKE ALL PASSED")
endif()
