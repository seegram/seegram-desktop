# Real credentials are supplied locally or through CI secrets.
# Upstream test builds keep using TDESKTOP_API_TEST.
if (TDESKTOP_API_TEST)
    return()
endif()

if (NOT "$ENV{TDESKTOP_API_ID}" STREQUAL "" OR NOT "$ENV{TDESKTOP_API_HASH}" STREQUAL "")
    if ("$ENV{TDESKTOP_API_ID}" STREQUAL "" OR "$ENV{TDESKTOP_API_HASH}" STREQUAL "")
        message(FATAL_ERROR "Set both TDESKTOP_API_ID and TDESKTOP_API_HASH environment variables.")
    endif()
    set(TDESKTOP_API_ID "$ENV{TDESKTOP_API_ID}" CACHE STRING "Telegram API ID" FORCE)
    set(TDESKTOP_API_HASH "$ENV{TDESKTOP_API_HASH}" CACHE STRING "Telegram API hash" FORCE)
else()
    include("${CMAKE_CURRENT_LIST_DIR}/private/api_credentials.cmake" OPTIONAL)
endif()
