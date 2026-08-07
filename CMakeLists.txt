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

add_library(mithril SHARED
    src/egl/egl_impl.cpp
    src/egl/egl_exports.cpp
    src/gl/gl_exports.cpp
    src/gl/gl_impl.cpp
    src/gl/gl_stub.cpp
    src/state/state.cpp
    src/util/log.cpp
)

target_include_directories(mithril PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
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
    set_source_files_properties(src/gl/gl_exports.cpp PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter")
endif()
