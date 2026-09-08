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

add_executable(plural_tests plural_tests.cpp)
target_include_directories(plural_tests PRIVATE "${telegram}/SourceFiles")
add_test(NAME profile_counter_plurals COMMAND plural_tests)

add_executable(history_layout_tests history_layout_tests.cpp)
target_include_directories(history_layout_tests PRIVATE "${telegram}/SourceFiles")
target_link_libraries(history_layout_tests PRIVATE Qt6::Core)
add_test(NAME history_scaled_layout COMMAND history_layout_tests)

add_executable(badge_svg_tests badge_svg_tests.cpp)
target_include_directories(badge_svg_tests PRIVATE "${telegram}/SourceFiles")
target_compile_definitions(badge_svg_tests PRIVATE BADGE_RESOURCE_DIR="${telegram}/Resources/fork/verifications")
target_link_libraries(badge_svg_tests PRIVATE Qt6::Core)
add_test(NAME remote_badge_svg_validation COMMAND badge_svg_tests)

add_executable(privacy_settings_tests privacy_settings_tests.cpp)
target_include_directories(privacy_settings_tests PRIVATE
    "${telegram}/SourceFiles"
    "${telegram}/lib_rpl"
    "${telegram}/lib_base"
    "${telegram}/ThirdParty/GSL/include")
add_test(NAME privacy_settings COMMAND privacy_settings_tests)

add_executable(account_limits_tests account_limits_tests.cpp)
target_include_directories(account_limits_tests PRIVATE "${telegram}/SourceFiles")
add_test(NAME account_storage_limits COMMAND account_limits_tests)

add_executable(account_profile_policy_tests account_profile_policy_tests.cpp)
target_include_directories(account_profile_policy_tests PRIVATE "${telegram}/SourceFiles")
add_test(NAME account_profile_isolation_policy COMMAND account_profile_policy_tests)

add_executable(message_time_tests message_time_tests.cpp)
target_include_directories(message_time_tests PRIVATE "${telegram}/SourceFiles")
target_link_libraries(message_time_tests PRIVATE Qt6::Core)
add_test(NAME message_time_format COMMAND message_time_tests)

add_executable(seetg_features_tests seetg_features_tests.cpp)
target_include_directories(seetg_features_tests PRIVATE
    "${telegram}/SourceFiles"
    "${telegram}/lib_rpl"
    "${telegram}/lib_base"
    "${telegram}/ThirdParty/GSL/include")
add_test(NAME seetg_feature_switches COMMAND seetg_features_tests)

add_executable(gift_batch_tests gift_batch_tests.cpp)
target_include_directories(gift_batch_tests PRIVATE "${telegram}/SourceFiles")
add_test(NAME gift_batch_policy COMMAND gift_batch_tests)
