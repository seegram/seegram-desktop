cmake_minimum_required(VERSION 3.25)
project(seegram_windows_disguise_tests LANGUAGES CXX)
if (NOT WIN32)
    message(FATAL_ERROR "These tests exercise the native Windows shell")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(repo "${CMAKE_CURRENT_LIST_DIR}/../../..")
add_executable(windows_disguise_tests
    windows_disguise_tests.cpp
    "${repo}/Telegram/SourceFiles/fork/disguise_shell_win.cpp")
target_include_directories(windows_disguise_tests PRIVATE
    "${repo}/Telegram/SourceFiles"
    "${repo}/Telegram/ThirdParty/GSL/include")
target_compile_definitions(windows_disguise_tests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(windows_disguise_tests PRIVATE shell32 propsys ole32 uuid user32 gdi32)
enable_testing()
add_test(NAME windows_icons_and_shortcuts COMMAND windows_disguise_tests
    "${repo}/Telegram/Resources/art/icon256.ico"
    "${repo}/Telegram/Resources/art/disguise/telegram.ico")
