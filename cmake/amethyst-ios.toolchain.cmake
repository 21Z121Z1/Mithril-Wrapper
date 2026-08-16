# Amethyst iOS wrapper around leetal/ios-cmake.
#
# ios-cmake assigns CMAKE_SHARED_LINKER_FLAGS on every toolchain evaluation,
# including the later enable_language(OBJCXX) pass. Passing the isolation flags
# directly on the cmake command line (or injecting them after project()) is
# therefore not durable. This wrapper deliberately includes ios-cmake first and
# appends Mithril's isolation policy afterwards on *every* toolchain pass.

# Toolchain files are also evaluated in CMake try_compile() child projects.
# CMAKE_TRY_COMPILE_PLATFORM_VARIABLES is the canonical propagation path, while
# environment fallback makes the compiler-ABI probe robust on CMake/ios-cmake
# combinations that instantiate the child before those custom cache variables
# have been copied.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    MITHRIL_IOS_BASE_TOOLCHAIN
    MITHRIL_AMETHYST_EXPORT_LIST
    MITHRIL_AMETHYST_FORCE_NOT_WEAK_LIST)

foreach(_var IN ITEMS
        MITHRIL_IOS_BASE_TOOLCHAIN
        MITHRIL_AMETHYST_EXPORT_LIST
        MITHRIL_AMETHYST_FORCE_NOT_WEAK_LIST)
    if((NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "") AND
       DEFINED ENV{${_var}} AND NOT "$ENV{${_var}}" STREQUAL "")
        set(${_var} "$ENV{${_var}}")
    endif()
    if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
        message(FATAL_ERROR "${_var} must be supplied for Amethyst isolation")
    endif()
    if(NOT EXISTS "${${_var}}")
        message(FATAL_ERROR "${_var} does not exist: ${${_var}}")
    endif()
endforeach()

include("${MITHRIL_IOS_BASE_TOOLCHAIN}")

# Avoid duplicate appends when CMake evaluates the wrapper more than once in a
# scope that retained the previous value.
if(NOT CMAKE_SHARED_LINKER_FLAGS MATCHES "-exported_symbols_list")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS
        " -Wl,-exported_symbols_list,${MITHRIL_AMETHYST_EXPORT_LIST}")
endif()
if(NOT CMAKE_SHARED_LINKER_FLAGS MATCHES "-force_symbols_not_weak_list")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS
        " -Wl,-force_symbols_not_weak_list,${MITHRIL_AMETHYST_FORCE_NOT_WEAK_LIST}")
endif()

message(STATUS
    "Amethyst wrapper toolchain shared-link flags: ${CMAKE_SHARED_LINKER_FLAGS}")
