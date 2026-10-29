# ui.material_panel (archived: archive/tests/ui) — the material properties panel edits the MATERIAL, not
# just its Property list (the dead-panel regression, 2026-08-31). Compiles the
# panel slice with link stubs for Database/MaterialReader/SceneWriter
# (tests/ui/test_stubs.cpp) so the suite stays free of Sql and the io/ layer.
find_package(Qt6 REQUIRED COMPONENTS Widgets Test Sql)

# ui.viewport_cover is GONE with the widget it tested (owner decision D2,
# SPECS/STATS_OVERLAY_SPEC.md §6): the loading cover is drawn by the engine now,
# so there is no Qt widget left to unit-test offscreen. What it really guarded —
# the state machine that decides WHEN a cover is up — was always gated by
# scripting.e2e (renamed presentation_state), which survives unchanged, and the
# pixels are gated by test_engine's hud_overlay_* cases.

# ui.media_lazy (archived: archive/tests/ui) — Qt Multimedia stays off the startup path
# (STABILITY_PROGRAM_SPEC Lane 6a). VideoPreviewWidget and iris::Scene both
# used to construct a QMediaPlayer in their constructors, which loaded the
# ffmpeg backend and enumerated audio devices (pipewire/PulseAudio) on every
# launch and in every headless suite. Pins: nothing built until something is
# played, playing still decodes the fixture, stop()-before-play is safe.
find_package(Qt6 REQUIRED COMPONENTS Multimedia)

# view.window_on_render_thread — THE WINDOW SPIKE (ENGINE-UPGRADE-U3 stage 4's premise): every engine call on a
# second thread — create on Qt's Display + a view on a winId, three resizes, a float + re-dock (the native window
# replaced), an MSAA change, ~Engine on that thread — under the validation layer; 0 X errors, 0 validation errors.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_window_render_thread test_window_render_thread.cpp xerrorcounter.cpp)
    set_target_properties(test_window_render_thread PROPERTIES
        CXX_STANDARD 17
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_window_render_thread PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_include_directories(test_window_render_thread PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/include)
    target_link_libraries(test_window_render_thread PRIVATE JahshakaEngine Qt6::Widgets X11)
    add_dependencies(test_window_render_thread JahshakaEngineMedia)
    jah_gpu_row(view.window_on_render_thread SHIP CLASS engine FRAMES 120 COMMAND test_window_render_thread
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(view.window_on_render_thread PROPERTIES
        ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}QT_QPA_PLATFORM=xcb;VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
        FAIL_REGULAR_EXPRESSION "Validation Error;X ERROR"
        TIMEOUT 60 LABELS "ui")
    jah_tsan_render(view.window_on_render_thread)   # the render thread's TSan arm (U3 decision 47)
endif()

# ui.photon_panel (archived: archive/tests/ui) — the World panel's Photon section (GI_UNIFIED_SPEC.md §2 / P2).
#
# The unification's promise is a UI one ("one switch, engine picks the
# techniques, hide the settings it consumes"), so it is asserted on the REAL
# widget: how many rows it puts on the visible surface, that the disclosure
# hides the rest, and that every gesture writes through irisgl/document/scenegraph/worldmodes
# into the document instead of inventing panel state. Offscreen, no display.

# ---------------------------------------------------------------------------
# ui.vr_panel (archived: archive/tests/ui) — THE WORLD PANEL'S VR SECTION (lane VR-WORLD-1, owner request
# 2026-09-18: a place to set the project's VR settings).
#
# The ask is a UI one, so it is asserted on the REAL widget: one row per entry
# of the vrworld table, at the values the PROJECT holds, each gesture writing
# the document through the same sceneprops key `world.vr` writes and landing as
# exactly one undo step — plus the greying rule for a row the current turn mode
# makes meaningless. Offscreen, no display. Shares the photon panel's source
# list: the same blade spine and the same link stubs.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
# A document node is an engine node (SCENEGRAPH_SPEC D2): the headless graph.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ui.selection_cost (archived: archive/tests/ui) — a selection change costs the same at switch 200 as at
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
# THE PROPERTIES-COLUMN SLICE, named once (COMPONENTS-1). Two suites compile
# the WHOLE right column — ui.selection_cost (archived: archive/tests/ui), which measures what a pick costs,
# and ui.components (archived: archive/tests/ui), which drives the Components section — and an eighty-line
# source list copied twice is a list that stops agreeing with itself the first
# time a blade is added. Both targets take this variable; neither owns it.
set(JAH_PROPERTIES_COLUMN_SOURCES
    test_stubs.cpp
    test_selection_stubs.cpp
    # The row-undo spine (debt L6): every panel row's gesture and the command it
    # becomes.
    test_undo_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/rowundo.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/panelundo.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/scenepropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/nodeeditcommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/setnodepropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/structuralundo.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/scenenodepropertieswidget.cpp
    # COMPONENTS-1: the column's Components section and the icons it draws.
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/componentspropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/nodeicons.cpp
    ${CMAKE_SOURCE_DIR}/src/services/selectionservice.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/mobilitypropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertyrows.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/transformeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/materialpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/fogpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldcloudspropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldheightfogpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldgipropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/lookstackeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldpostfxpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldvrpropertywidget.cpp
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
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/cubemapwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/assetdrag.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/lightbindings.cpp
    ${CMAKE_SOURCE_DIR}/src/services/planarreflectors.cpp
    # Headers whose Q_OBJECT classes have no .cpp of their own in this target:
    # AUTOMOC only pairs a header with a source of the same name, so the two
    # signal-only classes the panels connect to have to be named here.
    ${CMAKE_SOURCE_DIR}/src/viewport/ieditorviewport.h
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.h
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/classicsheets.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/changematerialpropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/worldmodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    # COMPONENTS-1: the Components section pushes a click through the real
    # selection service, so the suites link it rather than a stub of it.
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/looks.cpp
    # the VR settings table the sceneprops rows are generated from (VR-WORLD-1)
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/propertywidgetbase.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/basepropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesome.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesomeAnim.cpp)

# THE STAGED ICONS, REACHABLE FROM A TEST BINARY (COMPONENTS-1). Every icon in
# this app is resolved against applicationDirPath (IrisUtils::getAbsoluteAssetPath)
# and a tests/ui binary does not live in bin/, where the icons are staged — so
# a suite that asserts anything about an icon would be asserting about null
# pixmaps. Made HERE, at configure, rather than by the suite at run time: a
# runtime call cannot replace a link that exists but dangles (a moved or
# rebuilt bin/), and this one is re-made on every configure. The suite still
# checks and fails loudly, because a link is not a guarantee.
file(CREATE_LINK "${CMAKE_BINARY_DIR}/bin/app" "${CMAKE_CURRENT_BINARY_DIR}/app"
     SYMBOLIC)

# ui.properties_width (archived: archive/tests/ui) — THE PROPERTIES PANEL FITS ITS DOCK (owner report
# 2026-09-08: "the settings run off the right side of the panel and I can't see
# some of them").
#
# The dock's scroll area has NO horizontal scrollbar, so a row that cannot
# shrink to the dock's width is not scrolled to — it is clipped away. The suite
# lays the REAL panel out at the three widths the column can have and asserts,
# for every kind of selection, that the content's minimum width fits the
# viewport and that no visible row's right edge falls outside it.
#
# Same slice, stubs and style as ui.selection_cost (archived: archive/tests/ui) (the row minimums ARE style
# metrics, so the theme has to be the real one). Offscreen, no display.
# THE PROPERTIES PANEL'S SOURCE SET, shared by the two suites that build the
# real panel offscreen (ui.properties_width (archived: archive/tests/ui) measures its width, ui.properties_tabs (archived: archive/tests/ui)
# drives its World | Selection tabs). One list, so a new blade never makes one of
# them link and the other not.
set(JAH_PROPERTIES_PANEL_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ui/controls/assetdrag.cpp
    test_stubs.cpp
    test_selection_stubs.cpp
    # The row-undo spine (debt L6): every panel row's gesture and the command it
    # becomes.
    test_undo_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/rowundo.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/panelundo.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/scenepropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/nodeeditcommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/setnodepropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/structuralundo.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/scenenodepropertieswidget.cpp
    # COMPONENTS-1: the column's Components section and the icons it draws.
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/componentspropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/nodeicons.cpp
    ${CMAKE_SOURCE_DIR}/src/services/selectionservice.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/mobilitypropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertyrows.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/transformeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/materialpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/fogpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldcloudspropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldheightfogpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldgipropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/lookstackeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldpostfxpropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/worldvrpropertywidget.cpp
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
    ${CMAKE_SOURCE_DIR}/src/ui/panels/propertywidgets/cubemapwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/accordionbladewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/rowfit.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/propertiestabstrip.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/lightbindings.cpp
    ${CMAKE_SOURCE_DIR}/src/services/planarreflectors.cpp
    # Headers whose Q_OBJECT classes have no .cpp of their own in this target:
    # AUTOMOC only pairs a header with a source of the same name, so the two
    # signal-only classes the panels connect to have to be named here.
    ${CMAKE_SOURCE_DIR}/src/viewport/ieditorviewport.h
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.h
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/classicsheets.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/changematerialpropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/worldmodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/looks.cpp
    # the VR settings table the sceneprops rows are generated from (VR-WORLD-1)
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/propertywidgetbase.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/propertywidgets/basepropertywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesome.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/qtawesome/QtAwesomeAnim.cpp)

# ui.tree_multiselect (archived: archive/tests/ui) — THE OUTLINER'S MODIFIER SEMANTICS (EDITOR_MULTISELECT_SPEC
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

# ui.column_law (archived: archive/tests/ui) — S1: every page's columns are the editor's columns, and the
# console is a TAB of the editor's bottom tray. Spawns the REAL binary and
# drives it over MCP (the shutdown suites' harness): these widths come out of a
# LAYOUT, which only happens in a shown window with the event loop turning
# between the show and the measurement — a --script run holds the loop for its
# whole run and cannot see them. Needs a display (the app is the engine app);
# scratch HOME + data root like every other app-spawning suite.
find_package(Qt6 REQUIRED COMPONENTS Core Network)
set(_columns_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-columns)
file(MAKE_DIRECTORY ${_columns_home}/run)

# The harness header names the app's binary for its spawn(), which this row never calls: no app, so not the app's
# path (source.gpu_rows_closure reads JAHSHAKA_BINARY = the app as "spawns the app").

# ui.space_docks — THE EDITOR COMES BACK WITH ITS PANELS (lane SPACE-1, owner
# report 2026-09-14). Same harness as ui.column_law (archived: archive/tests/ui), and for the same reason —
# these are LAYOUT facts, only visible in a shown window with the loop turning
# between the switch and the measurement — plus one thing no single-process
# suite can do: it quits the app FROM THE PLAYER SPACE and boots it again in the
# same data root, which is the only way to see what closeEvent really stored.
add_executable(test_space_docks test_space_docks.cpp)
set_target_properties(test_space_docks PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_space_docks PRIVATE Qt6::Core Qt6::Network)
target_include_directories(test_space_docks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_space_docks PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
set(_space_docks_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-space-docks)
file(MAKE_DIRECTORY ${_space_docks_home}/run)
jah_gpu_row(ui.space_docks WARM_HOME "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-space-docks" SHIP CLASS app TIER low NEEDS NONE COMMAND test_space_docks
         WORKING_DIRECTORY ${_space_docks_home}/run)
set_tests_properties(ui.space_docks PROPERTIES JAHSHAKA_TEST_FRAMES 631)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
set_tests_properties(ui.space_docks PROPERTIES
    ENVIRONMENT "HOME=${_space_docks_home};JAHSHAKA_DATA_ROOT=${_space_docks_home}/.local/share/Jahshaka"
    TIMEOUT 60 LABELS "ui")
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(ui.space_docks)   # spawns the app
jah_lsan_lane(ui.space_docks)   # spawns the app: its harness reads the child's exit (quitAndReap) — green = the app leaked nothing

set(_winmin_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-window-minimum)
file(MAKE_DIRECTORY ${_winmin_home}/run)

set(_matdrag_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-material-drag)
file(MAKE_DIRECTORY ${_matdrag_home}/run)

set(_shot_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-shot-aspect)
file(MAKE_DIRECTORY ${_shot_home}/run)

# ui.view_options_submenu (archived: archive/tests/ui) — THE VIEW OPTIONS SUBMENUS POP EVERY TIME (ATOM-MENU-POPUP-1, the
# owner's smoke 2026-10-19). Real pointer input at the editor's View Options button and its Atom
# View / Photon View rows, where the app reports them (app.widget, app.menus): a hover pops each
# submenu, a click on its row the moment the pointer arrives pops it (the press that used to
# strand it behind Qt's menu fade), and after a use the first hover pops it again; each mode it
# triggers is the view the world reads. TIER high NEEDS photon: the Atom view paints at High (Low
# is a passthrough viewport) and Photon's Voxels needs Photon. ITS OWN XVFB, like app.input_keys (archived: archive/tests/app)
# (the script says why: synthesised input goes to whatever is under the pointer), and registered
# only where the tools it drives exist. FRAMES 260 is an OBSERVATION under the Live render policy
# (the green runs rendered 260; the red run, which waits out its bounded polls, rendered 692).
# TIMEOUT 60 (URGENT #48: was 260) = the cold price 41.99 s x 6.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(JAH_XDOTOOL xdotool)
    find_program(JAH_CURL curl)
    find_program(JAH_JQ jq)
    find_program(JAH_XVFB Xvfb)
    find_program(JAH_XDPYINFO xdpyinfo)
endif()

# ui.mcp_prefs (archived: archive/tests/ui) — the Claude/MCP preferences page does not write back STALE
# state (round-2 review of MCP-LOG-1). OK saves every page of a dialog that is
# built once and shown many times, so a control still showing what it was
# built with silently reverts whatever changed in between: a scripted
# app.mcpLogging({session:true}), and a server started from --mcp-port (which
# an untouched OK used to STOP). The page is a slice with link stubs for the
# server, the window and the theme — no Sql, no engine, no MainWindow.
find_package(Qt6 REQUIRED COMPONENTS Widgets HttpServer)
# ---------------------------------------------------------------------------
# ui.presets_favorites (archived: archive/tests/ui) — CLOSE-2 (round 2, D1): the Models/Materials presets
# tabs read the library when their Database ARRIVES, not while they are being
# built. Both constructors used to call addFavorites() through an unassigned
# `AssetPanel::handle`; null-guarding that read alone would have silently
# emptied both tabs for every user. Link stubs for Database (the tests/ui
# idiom) — the subject is the call order, so a fake library that counts its
# reads proves more than a real one.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent)

# ui.import_dialog (archived: archive/tests/ui) — THE IMPORT DECISION, AS A DIALOG
# (SPECS/IMPORT_DIALOG_SPEC.md §8/§9). The dialog's product is an
# iris::ImportSettings record, so the load-bearing assertion is that the record
# its widgets emit is byte-identical (canonical JSON, and therefore bake key) to
# the one a verb call with the same values produces. Beside it: the live extent
# preview's arithmetic, the origin helpers, the suggestion line's states and the
# batch state machine, all driven through the dialog's static helpers — plus the
# real pre-read on the real fixtures, because the whole preview rests on a
# flags-0 parse leaving vertices in the FILE's units.
# Widgets + IrisGL only: no database, no engine, no display.
find_package(Qt6 REQUIRED COMPONENTS Widgets Test Concurrent)

if(TEST ui.space_docks)
    jah_fresh_home_fixture(ui.space_docks.fresh_home uiSpaceDocksFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-space-docks" WARM LABELS "ui")
    set_property(TEST ui.space_docks APPEND PROPERTY FIXTURES_REQUIRED uiSpaceDocksFreshHome)
endif()

# ui.streaming_indicator — THE TOOLBAR'S STREAM-IN INDICATOR SAYS WHAT IS OWED, READABLY (BUGS-7 stage 3): the real theme
# offscreen; shown with shaders, textures and the lighting arm owed (alone and together), hidden when nothing is; its
# painted text >= 4.5:1 on the toolbar's band (a plain label beside it — the base's indicator — printed as the control).
find_package(Qt6 REQUIRED COMPONENTS Widgets)
# (Its HOME and data root are temporary directories of its own: no scratch root under the build dir.)
