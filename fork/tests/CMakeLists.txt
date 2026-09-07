cmake_minimum_required(VERSION 3.25)
project(seegram_comments_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core)
enable_testing()

set(telegram "${CMAKE_CURRENT_SOURCE_DIR}/../../Telegram")
add_executable(comments_data_tests
    comments_data_tests.cpp
    "${telegram}/SourceFiles/fork/seetg/seetg_comments_data.cpp")
target_include_directories(comments_data_tests PRIVATE
    "${telegram}/SourceFiles"
    "${telegram}/lib_base"
    "${telegram}/ThirdParty/GSL/include")
target_link_libraries(comments_data_tests PRIVATE Qt6::Core)
target_precompile_headers(comments_data_tests PRIVATE <base/basic_types.h>)
add_test(NAME comments_data COMMAND comments_data_tests)
