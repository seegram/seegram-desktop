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
    # gcc in the rockylinux build image reports a false -Wrestrict inside Qt's
    # qcontainertools_impl.h - a memcpy of "18446744065119617024 or more
    # bytes", which is the optimizer losing a range, not a bug in Qt. It is
    # fatal only because DESKTOP_APP_SPECIAL_TARGET turns on -Werror, and that
    # lives in the cmake_helpers submodule, which this fork does not patch.
    target_compile_options(common_options INTERFACE -Wno-error=restrict)
endif()
