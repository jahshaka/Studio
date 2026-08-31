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
