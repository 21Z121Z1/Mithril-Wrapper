# Native smoke-test registration shared by local CTest and CI.
#
# Keep the renderer tests pointed at the artifact produced by this build. The
# repository-level output/ directory remains the default deliverable location
# for compatibility with existing scripts, while build/output contains stable
# aliases used only by tests. This prevents CTest from accidentally dlopening a
# stale artifact left by another build directory or platform lane.

if(NOT BUILD_TESTING OR MITHRIL_IOS)
    return()
endif()

set(_mithril_test_runtime_dir "${CMAKE_CURRENT_BINARY_DIR}/output")
set(_mithril_test_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/tests")
file(MAKE_DIRECTORY "${_mithril_test_runtime_dir}" "${_mithril_test_binary_dir}")

add_custom_command(TARGET mithril POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "${_mithril_test_runtime_dir}/libmithril.dylib"
        "${_mithril_test_runtime_dir}/libmithril.so"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "$<TARGET_FILE:mithril>"
        "${_mithril_test_runtime_dir}/libmithril.dylib"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "$<TARGET_FILE:mithril>"
        "${_mithril_test_runtime_dir}/libmithril.so"
    COMMENT "Preparing build-local Mithril runtime aliases for CTest"
    VERBATIM)

set(_mithril_vulkan_smokes
    contract_smoke
    state_smoke
    shader_smoke
    draw_smoke
    texture_smoke
    uniform_array_smoke
    fbo_smoke
    3d_smoke
    render3d_smoke)

set(_mithril_directmetal_smokes
    state_smoke
    shader_smoke
    draw_smoke
    texture_smoke
    sampler_smoke
    matrix_uniform_smoke
    uniform_array_smoke
    provoking_vertex_smoke
    buffer_texture_smoke
    typed_vertex_smoke
    ubo_smoke
    query_smoke
    sync_smoke
    3d_smoke
    render3d_smoke
    fbo_smoke
    directmetal_fbo_smoke)

if(APPLE)
    set(_mithril_enabled_smokes ${_mithril_directmetal_smokes})
    set(_mithril_smoke_labels "directmetal;smoke")
    set(_mithril_smoke_environment
        "MITHRIL_BACKEND=metal;MITHRIL_EXPECT_RENDERER=Mithril DirectMetal;MITHRIL_LIBRARY=./output/libmithril.dylib")
elseif(UNIX)
    set(_mithril_enabled_smokes ${_mithril_vulkan_smokes})
    set(_mithril_smoke_labels "vulkan;smoke")
    set(_mithril_smoke_environment
        "MITHRIL_BACKEND=vulkan;MITHRIL_LIBRARY=./output/libmithril.so")
else()
    set(_mithril_enabled_smokes)
endif()

foreach(_smoke IN LISTS _mithril_enabled_smokes)
    set(_target "mithril_test_${_smoke}")
    add_executable(${_target} "${CMAKE_CURRENT_SOURCE_DIR}/tests/${_smoke}.c")
    set_target_properties(${_target} PROPERTIES
        OUTPUT_NAME "${_smoke}"
        RUNTIME_OUTPUT_DIRECTORY "${_mithril_test_binary_dir}")
    target_compile_options(${_target} PRIVATE -Wall -Wextra)
    if(CMAKE_DL_LIBS)
        target_link_libraries(${_target} PRIVATE ${CMAKE_DL_LIBS})
    endif()
    if(UNIX AND NOT APPLE AND
       (_smoke STREQUAL "3d_smoke" OR
        _smoke STREQUAL "render3d_smoke" OR
        _smoke STREQUAL "uniform_array_smoke"))
        target_link_libraries(${_target} PRIVATE m)
    endif()
    add_dependencies(${_target} mithril)

    add_test(NAME "${_smoke}" COMMAND "$<TARGET_FILE:${_target}>")
    set_tests_properties("${_smoke}" PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        ENVIRONMENT "${_mithril_smoke_environment}"
        LABELS "${_mithril_smoke_labels}"
        TIMEOUT 120)
endforeach()

unset(_mithril_enabled_smokes)
unset(_mithril_directmetal_smokes)
unset(_mithril_vulkan_smokes)
unset(_mithril_smoke_labels)
unset(_mithril_smoke_environment)
unset(_mithril_test_runtime_dir)
unset(_mithril_test_binary_dir)
