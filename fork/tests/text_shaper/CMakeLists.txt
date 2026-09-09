add_executable(seegram_text_shaper_tests EXCLUDE_FROM_ALL text_shaper_tests.cpp)
init_target(seegram_text_shaper_tests "(tests)")
target_link_libraries(seegram_text_shaper_tests PRIVATE
    desktop-app::lib_ui
    desktop-app::lib_crl
    desktop-app::external_qt
    Qt6::QOffscreenIntegrationPlugin
)
target_precompile_headers(seegram_text_shaper_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/Telegram/lib_ui/ui/ui_pch.h")
set_target_properties(seegram_text_shaper_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/seegram-tests")
if (APPLE)
    set_target_properties(seegram_text_shaper_tests PROPERTIES MACOSX_BUNDLE TRUE)
    add_custom_command(TARGET seegram_text_shaper_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:seegram_text_shaper_tests>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/lib_ui.rcc"
            "$<TARGET_BUNDLE_CONTENT_DIR:seegram_text_shaper_tests>/Resources/")
endif()
