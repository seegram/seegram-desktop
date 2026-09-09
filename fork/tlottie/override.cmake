# Temporary dependency repair, isolated from upstream source and submodules.
# A new upstream pin bypasses our patch and must pass the same C API probe.
function(seegram_prepare_tlottie)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(directory "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(output "${CMAKE_BINARY_DIR}/fork/tlottie")
    set(targets)
    if (APPLE)
        set(architectures "${DESKTOP_APP_MAC_ARCH}")
        if (NOT architectures)
            set(architectures "${CMAKE_OSX_ARCHITECTURES}")
        endif()
        if (NOT architectures)
            set(architectures "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        endif()
        foreach(arch IN LISTS architectures)
            if (arch STREQUAL "arm64")
                list(APPEND targets --target aarch64-apple-darwin)
            elseif (arch STREQUAL "x86_64")
                list(APPEND targets --target x86_64-apple-darwin)
            else()
                message(FATAL_ERROR "Unsupported tlottie macOS architecture: ${arch}")
            endif()
        endforeach()
    elseif (WIN32)
        if (build_winarm)
            list(APPEND targets --target aarch64-pc-windows-msvc)
        elseif (build_win64)
            list(APPEND targets --target x86_64-win7-windows-msvc)
        else()
            list(APPEND targets --target i686-win7-windows-msvc)
        endif()
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            list(APPEND targets --target aarch64-unknown-linux-gnu)
        elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            list(APPEND targets --target x86_64-unknown-linux-gnu)
        else()
            message(FATAL_ERROR "Unsupported tlottie Linux architecture")
        endif()
    else()
        message(FATAL_ERROR "Unsupported tlottie platform; review fork/tlottie")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${directory}/prepare.py"
        "${directory}/gradient-work.patch"
        "${directory}/gradient-regression.cpp"
        "${CMAKE_SOURCE_DIR}/Telegram/build/prepare/prepare.py"
    )
    file(MAKE_DIRECTORY "${output}")
    message(STATUS "Checking tlottie dependency (first patched build may take a minute)")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${directory}/prepare.py"
            --output "${output}" --libraries "${libs_loc}" ${targets}
        RESULT_VARIABLE prepared
        OUTPUT_FILE "${output}/prepare.log"
        ERROR_FILE "${output}/prepare.log"
    )
    if (NOT prepared EQUAL 0)
        message(FATAL_ERROR "tlottie preparation failed; see ${output}/prepare.log")
    endif()
    include("${output}/selected.cmake")
    if (seegram_tlottie_library)
        set_target_properties(external_tlottie_native PROPERTIES
            IMPORTED_LOCATION "${seegram_tlottie_library}"
            IMPORTED_LOCATION_DEBUG "${seegram_tlottie_library}"
            IMPORTED_LOCATION_RELEASE "${seegram_tlottie_library}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${seegram_tlottie_library}"
            IMPORTED_LOCATION_MINSIZEREL "${seegram_tlottie_library}"
        )
    else()
        get_target_property(seegram_tlottie_library external_tlottie_native IMPORTED_LOCATION)
        message(STATUS "New tlottie pin: temporary patch disabled, checking upstream library")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${seegram_tlottie_library}")
    # Key the check by actual archive bytes, not by the configured version.
    # This catches stale prepared libraries after an upstream update as well.
    file(SHA256 "${seegram_tlottie_library}" library_hash)
    file(SHA256 "${directory}/gradient-regression.cpp" test_hash)
    set(passed "${output}/passed-${library_hash}-${test_hash}-${CMAKE_HOST_SYSTEM_PROCESSOR}")
    if (NOT EXISTS "${passed}")
        # Rust's Windows archive uses the static release CRT. On macOS the
        # normal release archive may be universal; run the native test slice.
        set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
        set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
        unset(gradient_result CACHE)
        unset(gradient_compiled CACHE)
        try_run(gradient_result gradient_compiled
            "${output}/probe"
            "${directory}/gradient-regression.cpp"
            CMAKE_FLAGS
                "-DCMAKE_CXX_STANDARD=17"
                "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_HOST_SYSTEM_PROCESSOR}"
                "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
            LINK_LIBRARIES external_tlottie
            COMPILE_OUTPUT_VARIABLE compile_log
            RUN_OUTPUT_VARIABLE run_log
        )
        file(WRITE "${output}/regression.log" "${compile_log}\n${run_log}")
        if (NOT gradient_compiled OR NOT "${gradient_result}" STREQUAL "0")
            message(FATAL_ERROR "tlottie gradient regression failed. Review the upstream update and fork/tlottie; see ${output}/regression.log")
        endif()
        file(WRITE "${passed}" "${run_log}")
    endif()
    message(STATUS "tlottie gradient regression passed")
endfunction()

seegram_prepare_tlottie()
