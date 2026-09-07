cmake_minimum_required(VERSION 3.21)
project(SeeGramUpdaterCheck LANGUAGES CXX RC)
get_filename_component(seegram_root "../../.." ABSOLUTE)
if(NOT BASE_SOURCE_DIR)
    set(BASE_SOURCE_DIR "${seegram_root}/Telegram/lib_base")
endif()
add_executable(Updater WIN32
    "${seegram_root}/Telegram/SourceFiles/_other/updater_win.cpp"
    "${BASE_SOURCE_DIR}/base/platform/win/base_windows_safe_library.cpp")
target_compile_features(Updater PRIVATE cxx_std_20)
target_compile_definitions(Updater PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(Updater PRIVATE "${BASE_SOURCE_DIR}")
target_link_libraries(Updater PRIVATE user32 advapi32 shell32 ole32 shlwapi)
include("${seegram_root}/fork/windows_updater.cmake")
