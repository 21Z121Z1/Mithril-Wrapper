# Mithril-Wrapper - MG_Test/CMakeLists.txt
# Google Test unit test infrastructure for the Mithril-Wrapper pure-logic
# subsystems (state machine, GLSL->SPIR-V translation, GL format mapping,
# EGL config matching). Built only when MITHRIL_BUILD_TEST=ON.
#
# Layout mirrors MobileGL's MobileGL/MG_Test/ (one test file per subsystem,
# FetchContent for Google Test v1.17.0, gtest_main as the entry point).
#
# NOTE: FetchContent_Declare() requires network access to clone googletest
# from GitHub. In a sandbox without network access, CMake configuration will
# fail at the FetchContent_MakeAvailable() step with a git clone error — this
# is expected and is handled by the CI runner, which provides network. Local
# no-network configuration failures here can be safely ignored; the rest of
# the build system (the iOS dylib target) is unaffected because
# MITHRIL_BUILD_TEST defaults to OFF.

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
)
# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Vulkan headers are required for <vulkan/vulkan.h> pulled in by Backend.h /
# Resources.h / State.h. On iOS the production build sources them from
# MoltenVK-Headers; the Linux test build uses the system Vulkan SDK (installed
# in CI via LunarG installer / apt). find_package(Vulkan) requires BOTH the
# headers AND libvulkan.so, which is overkill for the test target — we never
# call into the Vulkan loader (BackendStub provides the backend_* symbols).
# Use find_path so a headers-only install (e.g. Vulkan-Headers package, or a
# VULKAN_HEADERS_INCLUDE_DIR hint) is sufficient.
find_package(Vulkan QUIET)
if(Vulkan_FOUND AND Vulkan_INCLUDE_DIRS)
    set(MITHRIL_TEST_VULKAN_INCLUDE_DIR "${Vulkan_INCLUDE_DIRS}")
else()
    find_path(MITHRIL_TEST_VULKAN_INCLUDE_DIR
        NAMES vulkan/vulkan.h
        PATHS
            "${VULKAN_HEADERS_INCLUDE_DIR}"
            "$ENV{VULKAN_SDK}/include"
            "$ENV{VULKAN_HEADERS_INCLUDE_DIR}"
            /usr/include
            /usr/local/include
        REQUIRED
    )
endif()

# ---- Sources under test (pure-logic, no MoltenVK / ObjC++ / iOS framework deps) ----
set(MITHRIL_TEST_SOURCES
    # gtest main replacement (placeholder for future global init hooks)
    main.cpp
    # backend_* C API stubs (return safe defaults; isolate from real Vulkan)
    BackendStub.cpp
    # Subsystems under test
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_State/State.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/Shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/Buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/Framebuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/VertexArray.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/Program.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/lookup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/EGLConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Backend/DirectVulkan/FormatMap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Backend/DirectVulkan/Reflect.cpp
    # Log.cpp: pulled in because Shader.cpp (and State.h via includes.h)
    # reference mithril::log_write via the MITHRIL_LOG_* macros. Log.cpp is
    # pure C++ (stderr + std::getenv) — no Vulkan / ObjC / iOS dependencies.
    ${CMAKE_CURRENT_SOURCE_DIR}/../MG_Impl/Log.cpp
    # egl/egl.cpp: the EGL 1.5 core implementation. Compiled into the test
    # binary so the new EGL 1.5 entry points (eglCreateSync, eglCreateImage,
    # eglCreatePlatformWindowSurface, ...) are linked in and exercisable by
    # EGLSyncTest / EGLImageTest / EGLPlatformSurfaceTest. egl.cpp is
    # cross-platform C++ (no platform-specific includes); it dispatches
    # surface creation to surface_create() / surface_get_size() extern
    # declarations provided by the platform-specific Surface*.cpp file below.
    # Its backend_* dependencies (backend_init, backend_create_swapchain, ...)
    # are satisfied by BackendStub.cpp; mithril::g_state comes from State.cpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/../egl/egl.cpp
    # Unit test files (one per subsystem under test).
    StateTest.cpp
    ShaderTest.cpp
    FormatMapTest.cpp
    EGLConfigTest.cpp
    # EGL 1.5 state-layer unit tests (shadow implementations; no real Vulkan).
    EGLSyncTest.cpp
    EGLImageTest.cpp
    EGLPlatformSurfaceTest.cpp
    BufferTest.cpp
    TextureTest.cpp
    FramebufferTest.cpp
    VertexArrayTest.cpp
    ProgramTest.cpp
    LookupTest.cpp
    ReflectTest.cpp
)

# Linux surface helper for egl.cpp's eglCreateWindowSurface path. Provides the
# surface_create() / surface_get_size() externs declared by egl.cpp; resolves
# libX11 lazily via dlopen so the test binary has no hard X11 link dependency.
# In the test environment libX11 may be absent, in which case surface_create
# returns nullptr and egl.cpp's eglCreateWindowSurface handles it as an error
# path. ${CMAKE_DL_LIBS} (linked below) covers the dlopen/dlsym calls.
if(UNIX AND NOT APPLE)
    list(APPEND MITHRIL_TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../egl/SurfaceX11.cpp
    )
endif()

add_executable(mithril_tests ${MITHRIL_TEST_SOURCES})

target_link_libraries(mithril_tests PRIVATE
    GTest::gtest_main
    # glslang + SPIRV-Cross: mirror the production target's link list so
    # Shader.cpp (glslang::InitializeProcess / GlslangToSpv) links cleanly.
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits
    spirv-cross-core
    # Linux: dladdr / dlsym used by the lookup path (and glslang internally).
    ${CMAKE_DL_LIBS}
)

target_include_directories(mithril_tests PRIVATE
    # Mithril-Wrapper-cpp/ root — every internal header is referenced relative
    # to here (e.g. "MG_Impl/Shader.h", "MG_Backend/Backend.h").
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    # Mithril-Wrapper-cpp/include/ — public GL / EGL headers.
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    # SPIRV-Cross + SPIRV-Headers includes (mirrors the production target).
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/SPIRV-Cross
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/SPIRV-Headers/include
    # Vulkan headers (resolved above; on Linux this is the system Vulkan SDK
    # or a VULKAN_HEADERS_INCLUDE_DIR hint).
    ${MITHRIL_TEST_VULKAN_INCLUDE_DIR}
)

# Reasonable warnings for the test target (mirror the production target).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mithril_tests PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable)
endif()

# Export the test executable's global symbol table so dlsym(RTLD_DEFAULT)
# (used by lookup.cpp's glXGetProcAddress) can resolve the GL entry points
# compiled into the test binary (glGenBuffers etc. from Buffer.cpp). Without
# -rdynamic the ELF executable only exports symbols needed for dynamic
# linking, and LookupTest's dlsym("glGenBuffers") returns NULL.
if(UNIX AND NOT APPLE)
    target_link_options(mithril_tests PRIVATE -rdynamic)
endif()

# Register with CTest so `ctest --output-on-failure` discovers the binary.
add_test(NAME mithril_tests COMMAND mithril_tests)
