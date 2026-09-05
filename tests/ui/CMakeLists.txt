# ui.material_panel — the material properties panel edits the MATERIAL, not
# just its Property list (the dead-panel regression, 2026-08-31). Compiles the
# panel slice with link stubs for Database/MaterialReader/SceneWriter
# (tests/ui/test_stubs.cpp) so the suite stays free of Sql and the io/ layer.
find_package(Qt6 REQUIRED COMPONENTS Widgets Test Sql)
add_executable(test_material_panel
    test_material_panel.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/materialpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/accordionbladewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/hfloatsliderwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorvaluewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/texturepickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/assetpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/checkboxwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/comboboxwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/textinputwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/labelwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/filepickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/dragspinbox.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/transformeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/cubemapwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/changematerialpropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/propertywidgetbase.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/basepropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesome.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesomeAnim.cpp)
set_target_properties(test_material_panel PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# AUTOUIC looks for .ui files beside the including source; the controls' .ui
# files live in src/ui/controls.
set_property(TARGET test_material_panel PROPERTY AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src/ui/controls
    ${CMAKE_SOURCE_DIR}/src/ui/panels
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets)
target_include_directories(test_material_panel PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_material_panel PRIVATE IrisGL Qt6::Widgets Qt6::Test Qt6::Sql)
add_test(NAME ui.material_panel COMMAND test_material_panel
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.material_panel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "ui")

# ui.property_anim — the keyframe edit service (src/services/animationedits.h):
# the track factory returns nullptr, never an indeterminate pointer, for
# property types it has no track shape for (ENGINEERING_DEBT_SPEC item 8: the
# createPropertyAnim release-build UB), and the write/remove operations the
# Timeline panel and the anim.* verbs SHARE behave the same for both callers.
# Document code only: no widgets, no display.
add_executable(test_property_anim test_property_anim.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationedits.cpp)
set_target_properties(test_property_anim PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_property_anim PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_property_anim PRIVATE IrisGL Qt6::Test)
add_test(NAME ui.property_anim COMMAND test_property_anim
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.property_anim PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "ui")

# ui.viewport_cover is GONE with the widget it tested (owner decision D2,
# SPECS/STATS_OVERLAY_SPEC.md §6): the loading cover is drawn by the engine now,
# so there is no Qt widget left to unit-test offscreen. What it really guarded —
# the state machine that decides WHEN a cover is up — was always gated by
# scripting.e2e (renamed presentation_state), which survives unchanged, and the
# pixels are gated by test_engine's hud_overlay_* cases.

# ui.media_lazy — Qt Multimedia stays off the startup path
# (STABILITY_PROGRAM_SPEC Lane 6a). VideoPreviewWidget and iris::Scene both
# used to construct a QMediaPlayer in their constructors, which loaded the
# ffmpeg backend and enumerated audio devices (pipewire/PulseAudio) on every
# launch and in every headless suite. Pins: nothing built until something is
# played, playing still decodes the fixture, stop()-before-play is safe.
find_package(Qt6 REQUIRED COMPONENTS Multimedia)
add_executable(test_media_lazy test_media_lazy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/videopreviewwidget.cpp)
set_target_properties(test_media_lazy PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_media_lazy PRIVATE
    JAHSHAKA_TINY_MP4="${CMAKE_SOURCE_DIR}/tests/scripting/fixtures/tiny.mp4")
target_include_directories(test_media_lazy PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_media_lazy
    PRIVATE IrisGL Qt6::Widgets Qt6::Multimedia Qt6::Test)
add_test(NAME ui.media_lazy COMMAND test_media_lazy
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.media_lazy PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "ui")

# ui.engine_view_winid — the View is rebuilt when Qt hands the widget a NEW
# native window (deep audit area 7). The ONE suite that drives EngineViewWidget
# itself, and the only Studio-side suite that needs a real X display and the
# xcb plugin: the whole subject is native window handles.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_engine_view_winid test_engine_view_winid.cpp
        ${CMAKE_SOURCE_DIR}/src/viewport/engineviewwidget.cpp)
    set_target_properties(test_engine_view_winid PROPERTIES
        AUTOMOC ON
        CXX_STANDARD 17
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_engine_view_winid PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_include_directories(test_engine_view_winid PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/irisgl/engine/include)
    target_link_libraries(test_engine_view_winid
        PRIVATE JahshakaEngine Qt6::Widgets Qt6::Test)
    add_dependencies(test_engine_view_winid JahshakaEngineMedia)
    add_test(NAME ui.engine_view_winid COMMAND test_engine_view_winid
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(ui.engine_view_winid PROPERTIES
        ENVIRONMENT "DISPLAY=${JAH_TEST_DISPLAY};QT_QPA_PLATFORM=xcb"
        TIMEOUT 180 LABELS "ui")
endif()

# The leak lane (scripts/sanitize.sh lsan): the offscreen widget suites — panel
# and dialog ownership (a missing parent is the classic Qt leak). The on-screen
# ui.engine_view_winid suite links the engine and stays out.
# ui.material_panel is QUARANTINED (L1 iris::Material properties — 54
# materials' worth). Its other leak, one QProxyStyle per slider row, was
# this lane's first fix (hfloatsliderwidget.cpp).
jah_lsan_lane(ui.property_anim ui.media_lazy)
jah_lsan_blocked(ui.material_panel)
