# ui.material_panel — the material properties panel edits the MATERIAL, not
# just its Property list (the dead-panel regression, 2026-08-31). Compiles the
# panel slice with link stubs for Database/MaterialReader/SceneWriter
# (tests/ui/test_stubs.cpp) so the suite stays free of Sql and the io/ layer.
find_package(Qt6 REQUIRED COMPONENTS Widgets Test Sql)
add_executable(test_material_panel
    test_material_panel.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/materialpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/accordionbladewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/rowfit.cpp
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
    ${CMAKE_SOURCE_DIR}/src/ui/controls/dragvaluewidgets.cpp
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
        ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}QT_QPA_PLATFORM=xcb"
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

# ui.light_channels — the lighting-channel checkbox row. Eight boxes over a
# 32-bit field: the test is about the 24 bits the row does NOT show (they must
# survive every edit), about "All" restoring the true all-ones default, and
# about setMask being quiet (the panel calls it while selecting a node).
# Widgets only — no document, no engine, no display.
add_executable(test_light_channels test_light_channels.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/lightchannelswidget.cpp)
set_target_properties(test_light_channels PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_light_channels PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_light_channels PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME ui.light_channels COMMAND test_light_channels
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.light_channels PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "ui")

# ui.drag_owner — ONE DRAG OWNER PER ITEM VIEW (src/ui/panels/singledragowner.h).
# The structural end of the "sticky preset" defect (third recurrence,
# 2026-09-07): the panels run their own QDrag from an event filter while the
# view's built-in machinery stayed armed underneath, so one gesture had two
# owners and the view's DraggingState outlived the drag. Red-first: the first
# block proves the armed view enters and keeps that state; the rest proves the
# two calls that end it. Widgets only — no document, no engine, no display.
add_executable(test_drag_owner test_drag_owner.cpp)
set_target_properties(test_drag_owner PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_drag_owner PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_drag_owner PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME ui.drag_owner COMMAND test_drag_owner
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.drag_owner PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "ui")

# ui.rayon_panel — the World panel's Rayon section (GI_UNIFIED_SPEC.md §2 / P2).
#
# The unification's promise is a UI one ("one switch, engine picks the
# techniques, hide the settings it consumes"), so it is asserted on the REAL
# widget: how many rows it puts on the visible surface, that the disclosure
# hides the rest, and that every gesture writes through services/worldmodes
# into the document instead of inventing panel state. Offscreen, no display.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(test_rayon_panel
    test_rayon_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldgipropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/services/looks.cpp
    ${CMAKE_SOURCE_DIR}/src/services/worldmodes.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gibounds.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/accordionbladewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/rowfit.cpp
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
    ${CMAKE_SOURCE_DIR}/src/ui/controls/dragvaluewidgets.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/transformeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/cubemapwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
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
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesomeAnim.cpp
    test_stubs.cpp)
set_target_properties(test_rayon_panel PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_property(TARGET test_rayon_panel PROPERTY AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src/ui/controls
    ${CMAKE_SOURCE_DIR}/src/ui/panels
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets)
target_include_directories(test_rayon_panel PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_rayon_panel PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
# A document node is an engine node (SCENEGRAPH_SPEC D2): the headless graph.
jah_document_graph(test_rayon_panel)
add_test(NAME ui.rayon_panel COMMAND test_rayon_panel
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.rayon_panel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "ui")
jah_no_display(ui.rayon_panel)

# ui.selection_cost — a selection change costs the same at switch 200 as at
# switch 1 (the 1 fps / 2-7 s stall regression, owner session 2026-09-08).
#
# The subject is the REAL properties panel under the REAL Qlementine style: the
# regression lived in the interaction between SceneNodePropertiesWidget's
# blade recycling (setParent(nullptr) per selection) and the focus-frame event
# filter that watches ancestors, and neither half is visible without the other.
# So this target links the whole property-panel slice — every blade the panel
# builds in its constructor — with the same link stubs the other panel suites
# use for the Sql/io layer. Offscreen, no display; the document nodes need the
# headless graph like every other document suite.
add_executable(test_selection_cost
    test_selection_cost.cpp
    test_stubs.cpp
    test_selection_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/scenenodepropertieswidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/transformeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/materialpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/fogpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldskypropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldgipropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/lookstackeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldpostfxpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldaapropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldmodespropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldshadowpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/camerapostfxpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/lightpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/decalpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/emitterpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/meshpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/physicspropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/skypropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/shaderpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/cubemapwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/accordionbladewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/rowfit.cpp
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
    ${CMAKE_SOURCE_DIR}/src/ui/controls/dragvaluewidgets.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/lightchannelswidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/particlerampwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/services/sunlink.cpp
    ${CMAKE_SOURCE_DIR}/src/services/lightbindings.cpp
    ${CMAKE_SOURCE_DIR}/src/services/planarreflectors.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/sunlightlinkcommand.cpp
    # Headers whose Q_OBJECT classes have no .cpp of their own in this target:
    # AUTOMOC only pairs a header with a source of the same name, so the two
    # signal-only classes the panels connect to have to be named here.
    ${CMAKE_SOURCE_DIR}/src/viewport/ieditorviewport.h
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.h
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/changematerialpropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/worldmodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/looks.cpp
    ${CMAKE_SOURCE_DIR}/src/services/worldmodes.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gibounds.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/propertywidgetbase.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/basepropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesome.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesomeAnim.cpp)
set_target_properties(test_selection_cost PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_property(TARGET test_selection_cost PROPERTY AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src/ui/controls
    ${CMAKE_SOURCE_DIR}/src/ui/panels
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets)
target_include_directories(test_selection_cost PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_selection_cost
    PRIVATE IrisGL Qt6::Widgets Qt6::Sql qlementine)
jah_document_graph(test_selection_cost)
add_test(NAME ui.selection_cost COMMAND test_selection_cost
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.selection_cost PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "ui")
jah_no_display(ui.selection_cost)


# ui.properties_width — THE PROPERTIES PANEL FITS ITS DOCK (owner report
# 2026-09-08: "the settings run off the right side of the panel and I can't see
# some of them").
#
# The dock's scroll area has NO horizontal scrollbar, so a row that cannot
# shrink to the dock's width is not scrolled to — it is clipped away. The suite
# lays the REAL panel out at the three widths the column can have and asserts,
# for every kind of selection, that the content's minimum width fits the
# viewport and that no visible row's right edge falls outside it.
#
# Same slice, stubs and style as ui.selection_cost (the row minimums ARE style
# metrics, so the theme has to be the real one). Offscreen, no display.
add_executable(test_properties_width
    test_properties_width.cpp
    test_stubs.cpp
    test_selection_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/scenenodepropertieswidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/transformeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/materialpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/fogpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldskypropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldgipropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/lookstackeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldpostfxpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldaapropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldmodespropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldshadowpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/camerapostfxpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/lightpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/decalpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/emitterpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/meshpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/physicspropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/skypropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/shaderpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/cubemapwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/accordionbladewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/rowfit.cpp
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
    ${CMAKE_SOURCE_DIR}/src/ui/controls/dragvaluewidgets.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/lightchannelswidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/particlerampwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/services/sunlink.cpp
    ${CMAKE_SOURCE_DIR}/src/services/lightbindings.cpp
    ${CMAKE_SOURCE_DIR}/src/services/planarreflectors.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/sunlightlinkcommand.cpp
    # Headers whose Q_OBJECT classes have no .cpp of their own in this target:
    # AUTOMOC only pairs a header with a source of the same name, so the two
    # signal-only classes the panels connect to have to be named here.
    ${CMAKE_SOURCE_DIR}/src/viewport/ieditorviewport.h
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.h
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/changematerialpropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/worldmodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/looks.cpp
    ${CMAKE_SOURCE_DIR}/src/services/worldmodes.cpp
    ${CMAKE_SOURCE_DIR}/src/services/gibounds.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/propertywidgetbase.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/basepropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesome.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesomeAnim.cpp)
set_target_properties(test_properties_width PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_property(TARGET test_properties_width PROPERTY AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src/ui/controls
    ${CMAKE_SOURCE_DIR}/src/ui/panels
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets)
target_include_directories(test_properties_width PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_properties_width
    PRIVATE IrisGL Qt6::Widgets Qt6::Sql qlementine)
jah_document_graph(test_properties_width)
add_test(NAME ui.properties_width COMMAND test_properties_width
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.properties_width PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "ui")
jah_no_display(ui.properties_width)


# ui.project_tile — THE OPEN PROJECT'S DARK BLUE CAPTION BAR (owner request
# 2026-09-08). Renders two real desktop tiles offscreen, one marked open, and
# asserts the bar colour IN PIXELS (QWidget::grab) in both themes: dark blue
# and readable under white text for the open tile, black for every other tile,
# black for BOTH under the archived Classic theme. Also pins the live path
# (setOpenProject / removeHighlight repaint an existing tile — the desktop
# rebuilds on open/close, the tile must not depend on it). Widgets only: no
# document, no database, no display.
add_executable(test_project_tile test_project_tile.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/itemgridwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/renameprojectdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_project_tile PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_property(TARGET test_project_tile PROPERTY AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs)
target_include_directories(test_project_tile PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_project_tile PRIVATE Qt6::Widgets Qt6::Test qlementine)
add_test(NAME ui.project_tile COMMAND test_project_tile
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.project_tile PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "ui")

# ui.dock_state — the editor's dock layout survives a restart (hygiene lane,
# 2026-09-09). The editor docks live in a NESTED QMainWindow whose saveState()
# was never called, so MainWindow's own windowState never described them. Drives
# shell/dockstate.h against a real QSettings file and a real nested QMainWindow
# built the way the shell builds one. Widgets only: no engine, no project,
# no display.
add_executable(test_dock_state test_dock_state.cpp)
set_target_properties(test_dock_state PROPERTIES
    AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_dock_state PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dock_state PRIVATE Qt6::Widgets)
add_test(NAME ui.dock_state COMMAND test_dock_state
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.dock_state PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "ui")
jah_no_display(ui.dock_state)

# ui.tree_multiselect — THE OUTLINER'S MODIFIER SEMANTICS (EDITOR_MULTISELECT_SPEC
# §2.2 / §5), on the real SceneHierarchyWidget with real QTest clicks.
#
# The subject is the press-time override of Qt's OWN Shift range: with
# ExtendedSelection Qt applies its range inside mousePressEvent, and the owner's
# rule (anchor = the topmost SELECTED row) has to pre-empt it. Nothing below the
# widget can show that, so the widget is what is linked — with the same Sql/io
# stub pattern the other panel suites use, plus stand-ins for the MainWindow
# entry points the panel's menu rows call (the suite wires no MainWindow, so
# none of them runs).
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(test_tree_multiselect
    test_tree_multiselect.cpp
    test_tree_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/scenehierarchywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/scenetreewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/services/planarreflectors.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/reparentscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/scenefoldercommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesome.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesomeAnim.cpp)
set_target_properties(test_tree_multiselect PROPERTIES
    AUTOMOC ON AUTOUIC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_property(TARGET test_tree_multiselect PROPERTY AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src/ui/controls
    ${CMAKE_SOURCE_DIR}/src/ui/panels
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets)
target_include_directories(test_tree_multiselect PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_tree_multiselect
    PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Test qlementine)
jah_document_graph(test_tree_multiselect)
add_test(NAME ui.tree_multiselect COMMAND test_tree_multiselect
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ui.tree_multiselect PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "ui")
jah_no_display(ui.tree_multiselect)
