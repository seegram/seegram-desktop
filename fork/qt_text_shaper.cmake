# Keep glyphs paired with their shaping font until lib_ui contains this fix.
# Generate the adjusted translation unit in the build tree, leaving the pinned
# submodule untouched. Refuse to silently apply it to changed upstream code.
if (DESKTOP_APP_USE_PANGO)
    return()
endif()

function(seegram_fix_qt_text_shaper)
    set(original "${CMAKE_SOURCE_DIR}/Telegram/lib_ui/ui/text/text_shaper_qt.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" source)

    macro(seegram_replace_text_fragment before after)
        string(FIND "${source}" "${before}" position)
        if (position EQUAL -1)
            message(FATAL_ERROR "lib_ui text shaper changed: review SeeGram's font-engine fix")
        endif()
        string(LENGTH "${before}" length)
        math(EXPR end "${position} + ${length}")
        string(SUBSTRING "${source}" ${end} -1 remainder)
        string(FIND "${remainder}" "${before}" duplicate)
        if (NOT duplicate EQUAL -1)
            message(FATAL_ERROR "Ambiguous lib_ui text-shaper fix: review the upstream source")
        endif()
        string(REPLACE "${before}" "${after}" source "${source}")
    endmacro()

    seegram_replace_text_fragment([=[
	QTextEngine *engine = nullptr;
]=] [=[
	QTextEngine *engine = nullptr;
	QExplicitlySharedDataPointer<QFontEngine> fontEngine;
]=])
    seegram_replace_text_fragment([=[
		const auto blockIt = _backend->engine.shapeGetBlock(firstItem + i);
		auto &si = e.layoutData->items[firstItem + i];
]=] [=[
		const auto blockIt = _backend->engine.shapeGetBlock(firstItem + i);
		auto &si = e.layoutData->items[firstItem + i];
		// Later blocks change the shared engine's font. Keep the engine that
		// produced these glyphs alive, including its fallback-font indices.
		_backend->entries[i].fontEngine = e.fontEngine(si);
]=])
    seegram_replace_text_fragment(
        "entry.engine->fontEngine(*entry.si)->getGlyphBearings("
        "entry.fontEngine->getGlyphBearings(")
    seegram_replace_text_fragment(
        "item.fontEngine = e.fontEngine(*entry.si);"
        "item.fontEngine = entry.fontEngine.data();")

    set(fixed "${CMAKE_CURRENT_BINARY_DIR}/seegram/text_shaper_qt.cpp")
    file(CONFIGURE OUTPUT "${fixed}" CONTENT "${source}" @ONLY)
    get_target_property(sources lib_ui SOURCES)
    if (NOT original IN_LIST sources)
        message(FATAL_ERROR "lib_ui no longer builds the expected Qt text shaper")
    endif()
    list(REMOVE_ITEM sources "${original}")
    set_property(TARGET lib_ui PROPERTY SOURCES "${sources}")
    target_sources(lib_ui PRIVATE "${fixed}")
endfunction()

seegram_fix_qt_text_shaper()

option(SEEGRAM_BUILD_TEXT_SHAPER_TESTS "Build the isolated text-shaping regression test" OFF)
if (SEEGRAM_BUILD_TEXT_SHAPER_TESTS)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/tests/text_shaper seegram_text_shaper_tests)
endif()
