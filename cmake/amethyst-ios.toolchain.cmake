# Amethyst iOS wrapper around leetal/ios-cmake.
#
# ios-cmake assigns CMAKE_SHARED_LINKER_FLAGS on every toolchain evaluation,
# including the later enable_language(OBJCXX) pass. Passing the isolation flags
# directly on the cmake command line (or injecting them after project()) is
# therefore not durable. This wrapper deliberately includes ios-cmake first and
# appends Mithril's isolation policy afterwards on *every* toolchain pass.

# Toolchain files are also evaluated in CMake try_compile() child projects.
# Explicitly forward the custom paths so compiler ABI probes see the same base
# toolchain and isolation inputs instead of failing before real configuration.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    MITHRIL_IOS_BASE_TOOLCHAIN
    MITHRIL_AMETHYST_EXPORT_LIST
    MITHRIL_AMETHYST_FORCE_NOT_WEAK_LIST)

if(NOT DEFINED MITHRIL_IOS_BASE_TOOLCHAIN OR
   "${MITHRIL_IOS_BASE_TOOLCHAIN}" STREQUAL "")
    message(FATAL_ERROR "MITHRIL_IOS_BASE_TOOLCHAIN is required")
endif()
if(NOT EXISTS "${MITHRIL_IOS_BASE_TOOLCHAIN}")
    message(FATAL_ERROR
        "Base iOS toolchain does not exist: ${MITHRIL_IOS_BASE_TOOLCHAIN}")
endif()

include("${MITHRIL_IOS_BASE_TOOLCHAIN}")

foreach(_var IN ITEMS
        MITHRIL_AMETHYST_EXPORT_LIST
        MITHRIL_AMETHYST_FORCE_NOT_WEAK_LIST)
    if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
        message(FATAL_ERROR "${_var} must be supplied for Amethyst isolation")
    endif()
    if(NOT EXISTS "${${_var}}")
        message(FATAL_ERROR "${_var} does not exist: ${${_var}}")
    endif()
endforeach()

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
