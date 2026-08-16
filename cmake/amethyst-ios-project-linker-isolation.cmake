# Opt-in linker isolation for the Amethyst iOS integration build.
#
# leetal/ios-cmake unconditionally assigns CMAKE_SHARED_LINKER_FLAGS while its
# toolchain file is being evaluated. Therefore a command-line
# -DCMAKE_SHARED_LINKER_FLAGS=... is overwritten before libmithril's link rule
# is generated. This file is loaded through CMAKE_PROJECT_mithril_INCLUDE,
# which CMake evaluates at the end of project(mithril), after the toolchain has
# finished. That gives us a deterministic post-toolchain injection point while
# leaving normal Mithril builds unchanged.

if(NOT APPLE)
    message(FATAL_ERROR "Amethyst linker isolation is Apple-only")
endif()

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

set(_mithril_amethyst_link_flags "${CMAKE_SHARED_LINKER_FLAGS}")
string(APPEND _mithril_amethyst_link_flags
    " -Wl,-exported_symbols_list,${MITHRIL_AMETHYST_EXPORT_LIST}")
string(APPEND _mithril_amethyst_link_flags
    " -Wl,-force_symbols_not_weak_list,${MITHRIL_AMETHYST_FORCE_NOT_WEAK_LIST}")

# Cache+FORCE is intentional here: ios-cmake has already populated the value;
# this opt-in project include is the authoritative final shared-library link
# policy for the isolated Amethyst build.
set(CMAKE_SHARED_LINKER_FLAGS "${_mithril_amethyst_link_flags}"
    CACHE STRING "Amethyst-isolated shared-library linker flags" FORCE)

message(STATUS
    "Amethyst iOS linker isolation active: ${CMAKE_SHARED_LINKER_FLAGS}")
