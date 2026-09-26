# Multiple-desktops storage tests (DESKTOPS_SPEC.md): builds the REAL
# src/data/database/database.cpp against a throwaway SQLite file — migration,
# desktop filter, move round-trip, freeform position round-trip. Headless
# (offscreen platform); never touches the user's live JahLibrary.db.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia Concurrent)
add_executable(test_desktops_db test_desktops_db.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    # database.cpp's link closure (Project, guids, asset helpers)
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationfile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rigsignature.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    # the project's VR settings table: the writer/reader are generated from it (VR-WORLD-1)
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_desktops_db PROPERTIES AUTOMOC ON)
target_include_directories(test_desktops_db PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_desktops_db PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME desktops.db COMMAND test_desktops_db
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(desktops.db PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktops")

# Slider-mode layout model (DESKTOP_SLIDER_SPEC.md): pure model, no widgets,
# no database — seeding, lossless round-trips, insert-at-drop ordering.
add_executable(test_slider_model test_slider_model.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/sliderlayoutmodel.cpp)
target_include_directories(test_slider_model PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_slider_model PRIVATE Qt6::Core)
add_test(NAME desktops.slider_model COMMAND test_slider_model
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(desktops.slider_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktops")

# desktops.grid_layout — DESKTOP-1 (owner smoke C, 2026-09-26): the REAL
# DynamicGrid built the way the boot builds it — shown FIRST, tiles added after
# (Qt shows a child of a visible parent through a queued call) — then the event
# loop settled by counted turns: every tile shown, sized, inside the viewport and
# overlapping none, in Rows (after a resize and a head insert too) and on
# Freeform's first entry (grid slots, never a pile). Widgets only (offscreen).
add_executable(test_grid_layout test_grid_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/dynamicgrid.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/itemgridwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/sliderlayoutmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/renameprojectdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/classicsheets.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_grid_layout PROPERTIES AUTOMOC ON AUTOUIC ON CXX_STANDARD 17)
set_property(TARGET test_grid_layout PROPERTY AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/src/ui/dialogs)
target_include_directories(test_grid_layout PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_grid_layout PRIVATE Qt6::Widgets Qt6::Concurrent qlementine)
add_test(NAME desktops.grid_layout COMMAND test_grid_layout
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(desktops.grid_layout PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "desktops")

# The leak lane (scripts/sanitize.sh lsan): database + model objects only.
jah_lsan_lane(desktops.db desktops.slider_model)

# ---------------------------------------------------------------------------
# DISPLAY-FREE, AND PROVEN (TEST_GATE_AUDIT.md §4.5). Every suite named here
# claims — in its own comment, or by running --headless, or by building nothing
# but a document — that it needs no X server. The claim was never enforced, so
# it drifted: a suite that quietly opened a display still passed on a developer
# box and would have failed the first CI runner without one, and every
# unnecessary X connection is one more thing four parallel jobs contend over.
# jah_no_display runs these with DISPLAY and WAYLAND_DISPLAY UNSET, which turns
# the claim into a gate. Verified 2026-09-09: each of these passes displayless
# with the same assertion count it has with a display.
jah_no_display(desktops.db desktops.slider_model desktops.grid_layout)
