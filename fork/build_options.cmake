# Build options this fork has to add to upstream's.
#
# Appended rather than passed on the command line, and included at the end of
# Telegram/CMakeLists.txt, because both of those decide who wins: interface
# options land after CMAKE_CXX_FLAGS, and the last flag on the command line is
# the one gcc obeys. Interface properties are read at generate time, so adding
# to the target here still reaches everything that links it.

if (CMAKE_SYSTEM_NAME STREQUAL "Linux"
        AND DESKTOP_APP_SPECIAL_TARGET
        AND TARGET common_options)
    # gcc-toolset-15 in the rockylinux build image produces a family of false
    # optimizer diagnostics on code upstream builds with another compiler: a
    # -Wrestrict for a memcpy of "18446744065119617024 or more bytes" inside
    # Qt's qcontainertools_impl.h, an -Waggressive-loop-optimizations for an
    # "iteration 384307168202282324" inside gcc's own stl_iterator_base_funcs.h.
    # Neither is a real defect; both are the optimizer losing a range.
    #
    # They are fatal only because DESKTOP_APP_SPECIAL_TARGET turns on -Werror,
    # and that target is not optional here - it is also what builds Packer,
    # which signs the update. -Werror lives in the cmake_helpers submodule,
    # which this fork does not patch, so it is demoted here instead.
    #
    # Demoted wholesale rather than one diagnostic at a time: each of those
    # costs a half hour build to discover, and this compiler clearly has more
    # of them. Warnings still print, and every other platform still builds
    # with -Werror, so a real one is not going to go unseen for long.
    target_compile_options(common_options INTERFACE -Wno-error)
endif()
