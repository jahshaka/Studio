cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(Jahshaka)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(OpenGL REQUIRED) # is this needed?
find_package(Qt5 REQUIRED COMPONENTS Concurrent Widgets Sql Svg)

include(UseModernCpp)
include(BuildType)

# set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/irisgl)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/breakpad)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/crash_handler)

set(SRCS
    src/main.cpp
    src/mainwindow.cpp 
    src/widgets/colorvaluewidget.cpp 
    src/widgets/colorpickerwidget.cpp 
    src/dialogs/preferencesdialog.cpp 
    src/dialogs/aboutdialog.cpp 
    src/dialogs/licensedialog.cpp 
    src/dialogs/preferences/worldsettings.cpp 
    src/dialogs/loadmeshdialog.cpp 
    src/widgets/animationwidget.cpp 
    src/widgets/keyframelabelwidget.cpp 
    src/widgets/keyframewidget.cpp 
    src/widgets/maintimelinewidget.cpp 
    src/widgets/timelinewidget.cpp 
    src/core/nodekeyframeanimation.cpp 
    src/core/project.cpp 
    src/core/settingsmanager.cpp 
    src/core/surfaceview.cpp 
    src/editor/editorcameracontroller.cpp 
    src/dialogs/infodialog.cpp 
    src/globals.cpp 
    src/dialogs/renamelayerdialog.cpp 
    src/widgets/sceneviewwidget.cpp 
    src/widgets/materialsets.cpp 
    src/widgets/modelpresets.cpp 
    src/widgets/accordianbladewidget.cpp 
    src/widgets/transformeditor.cpp 
    src/widgets/scenehierarchywidget.cpp 
    src/editor/cameracontrollerbase.cpp 
		src/editor/gizmo.cpp 
		src/editor/translationgizmo.cpp 
		src/editor/scalegizmo.cpp 
		src/editor/rotationgizmo.cpp 
    src/widgets/skypresets.cpp 
    src/editor/orbitalcameracontroller.cpp 
    src/widgets/scenenodepropertieswidget.cpp 
    src/widgets/propertywidgets/lightpropertywidget.cpp 
    src/widgets/propertywidgets/materialpropertywidget.cpp 
    src/widgets/propertywidgets/worldpropertywidget.cpp 
    src/io/scenewriter.cpp 
    src/core/thumbnailmanager.cpp 
    src/widgets/propertywidgets/fogpropertywidget.cpp 
    src/io/assetiobase.cpp 
    src/io/materialpresetreader.cpp 
    src/widgets/keyframelabeltreewidget.cpp 
    src/core/keyboardstate.cpp 
    src/io/scenereader.cpp 
    src/widgets/propertywidgets/emitterpropertywidget.cpp 
    src/widgets/propertywidgets/nodepropertywidget.cpp 
    src/editor/editorvrcontroller.cpp 
    src/widgets/propertywidgets/demopane.cpp 
    src/widgets/labelwidget.cpp 
    src/widgets/checkboxwidget.cpp 
    src/widgets/textinputwidget.cpp 
    src/widgets/comboboxwidget.cpp 
    src/widgets/hfloatsliderwidget.cpp 
    src/widgets/texturepickerwidget.cpp 
    src/io/materialreader.cpp 
    src/widgets/filepickerwidget.cpp 
    src/widgets/propertywidgets/meshpropertywidget.cpp 
    src/widgets/propertywidget.cpp 
    src/widgets/propertywidgets/postprocesspropertywidget.cpp 
    src/widgets/postprocesseswidget.cpp 
    src/dialogs/newprojectdialog.cpp 
    src/widgets/assetwidget.cpp 
    src/io/assetmanager.cpp 
    src/widgets/assetpickerwidget.cpp 
    src/widgets/keyframelabel.cpp 
    src/uimanager.cpp 
    src/widgets/keyframecurvewidget.cpp 
    src/dialogs/getnamedialog.cpp 
    src/widgets/createanimationwidget.cpp 
    src/core/database/database.cpp 
    src/core/database/projectdatabase.cpp 
    src/core/guidmanager.cpp 
    src/commands/transfrormscenenodecommand.cpp 
    src/commands/changematerialpropertycommand.cpp 
    src/commands/addscenenodecommand.cpp 
    src/commands/deletescenenodecommand.cpp 
    src/dialogs/progressdialog.cpp 
    src/widgets/screenshotwidget.cpp 
    src/widgets/projectmanager.cpp 
    src/widgets/dynamicgrid.cpp 
    src/widgets/itemgridwidget.cpp 
    src/dialogs/renameprojectdialog.cpp 
    src/editor/thumbnailgenerator.cpp 
    src/widgets/mainwindowviewport.cpp 
    src/editor/viewercontroller.cpp 
    src/dialogs/donatedialog.cpp
    src/widgets/assetview.cpp
    src/widgets/assetview.cpp
    src/widgets/assetviewer.cpp
    src/widgets/assetviewgrid.cpp
    src/widgets/assetgriditem.cpp
)

set(HEADERS
    src/mainwindow.h 
    src/dialogs/renamelayerdialog.h 
    src/widgets/colorvaluewidget.h 
    src/widgets/colorpickerwidget.h 
    src/helpers/settingshelper.h 
    src/dialogs/preferencesdialog.h 
    src/dialogs/aboutdialog.h 
    src/dialogs/licensedialog.h 
    src/dialogs/preferences/worldsettings.h 
    src/helpers/collisionhelper.h 
    src/dialogs/infodialog.h 
    src/dialogs/loadmeshdialog.h 
    src/widgets/animationwidget.h 
    src/widgets/keyframelabelwidget.h 
    src/widgets/keyframewidget.h 
    src/widgets/layertreewidget.h 
    src/widgets/maintimelinewidget.h 
    src/widgets/timelinewidget.h 
    src/core/keyframeanimation.h 
    src/core/keyframes.h 
    src/core/nodekeyframe.h 
    src/core/nodekeyframeanimation.h 
    src/core/project.h 
    src/core/settingsmanager.h 
    src/core/surfaceview.h 
    src/editor/editorcameracontroller.h 
    src/globals.h 
    src/widgets/sceneviewwidget.h 
    src/globals.h 
    src/widgets/materialsets.h 
    src/widgets/modelpresets.h 
    src/widgets/accordianbladewidget.h 
    src/widgets/transformeditor.h 
    src/widgets/scenehierarchywidget.h 
    src/editor/orbitalcameracontroller.h 
    src/editor/cameracontrollerbase.h 
    src/widgets/skypresets.h 
    src/widgets/propertywidgets/transformpropertywidget.h 
    src/widgets/propertywidgets/lightpropertywidget.h 
    src/widgets/propertywidgets/materialpropertywidget.h 
    src/widgets/scenenodepropertieswidget.h 
    src/widgets/propertywidgets/worldpropertywidget.h 
    src/io/scenewriter.h 
    src/io/scenereader.h 
    src/widgets/propertywidgets/scenepropertywidget.h 
    src/core/thumbnailmanager.h 
    src/widgets/propertywidgets/fogpropertywidget.h 
    src/core/meshmanager.h 
    src/io/assetiobase.h 
    src/core/materialpreset.h 
    src/io/materialpresetreader.h 
    src/widgets/keyframelabeltreewidget.h 
    src/io/materialpresetreader.h 
    src/editor/gizmo.h 
    src/editor/translationgizmo.h 
    src/editor/rotationgizmo.h 
    src/editor/scalegizmo.h 
    src/editor/gizmoinstance.h 
    src/core/keyboardstate.h 
    src/editor/editordata.h 
    src/widgets/propertywidgets/emitterpropertywidget.h 
    src/widgets/propertywidgets/nodepropertywidget.h 
    src/editor/editorvrcontroller.h 
    src/widgets/propertywidgets/demopane.h 
    src/widgets/labelwidget.h 
    src/widgets/checkboxwidget.h 
    src/widgets/textinputwidget.h 
    src/widgets/comboboxwidget.h 
    src/widgets/hfloatsliderwidget.h 
    src/widgets/texturepickerwidget.h 
    src/io/materialreader.hpp 
    src/widgets/filepickerwidget.h 
    src/widgets/propertywidgets/meshpropertywidget.h 
    src/widgets/propertywidget.h 
    src/widgets/basewidget.h 
    src/widgets/propertywidgets/postprocesspropertywidget.h 
    src/widgets/postprocesseswidget.h 
    src/dialogs/newprojectdialog.h 
    src/widgets/assetwidget.h 
    src/io/assetmanager.h 
    src/widgets/assetpickerwidget.h 
    src/constants.h 
    src/widgets/keyframelabel.h 
    src/uimanager.h 
    src/widgets/keyframecurvewidget.h 
    src/widgets/animationwidgetdata.h 
    src/dialogs/getnamedialog.h 
    src/widgets/createanimationwidget.h 
    src/core/database/database.h 
    src/core/database/projectdatabase.h 
    src/core/guidmanager.h 
    src/commands/transfrormscenenodecommand.h 
    src/commands/changematerialpropertycommand.h 
    src/commands/addscenenodecommand.h 
    src/commands/deletescenenodecommand.h 
    src/dialogs/progressdialog.h 
    src/commands/deleteanimationkeycommand.h 
    src/widgets/screenshotwidget.h 
    src/widgets/projectmanager.h 
    src/widgets/dynamicgrid.h 
    src/widgets/itemgridwidget.hpp 
    src/dialogs/renameprojectdialog.h 
    src/editor/thumbnailgenerator.h 
    src/widgets/mainwindowviewport.h 
    src/editor/viewercontroller.h 
    src/dialogs/donatedialog.h
    src/widgets/assetview.h
    src/widgets/assetviewer.h
    src/widgets/assetviewgrid.h
    src/widgets/assetgriditem.h
	src/breakpad/breakpad.h
)

# Todo - trim these down to nothing eventually
set(FORMS
    src/dialogs/renamelayerdialog.ui 
    src/widgets/colorvaluewidget.ui 
    src/widgets/colorpickerwidget.ui 
    src/dialogs/preferencesdialog.ui 
    src/dialogs/aboutdialog.ui 
    src/dialogs/licensedialog.ui 
    src/dialogs/preferences/worldsettings.ui 
    src/dialogs/infodialog.ui 
    src/dialogs/loadmeshdialog.ui 
    src/widgets/animationwidget.ui 
    src/widgets/maintimelinewidget.ui 
    src/widgets/materialsets.ui 
    src/widgets/accordianbladewidget.ui 
    src/widgets/transformeditor.ui 
    src/widgets/scenehierarchywidget.ui 
    src/widgets/skypresets.ui 
    src/widgets/keyframelabelwidget.ui 
    src/widgets/keyframelabeltreewidget.ui 
    src/widgets/modelpresets.ui 
    src/mainwindow.ui 
    src/widgets/labelwidget.ui 
    src/widgets/checkboxwidget.ui 
    src/widgets/textinputwidget.ui 
    src/widgets/comboboxwidget.ui 
    src/widgets/hfloatsliderwidget.ui 
    src/widgets/texturepickerwidget.ui 
    src/widgets/filepickerwidget.ui 
    src/widgets/propertywidget.ui 
    src/widgets/postprocesseswidget.ui 
    src/dialogs/newprojectdialog.ui 
    src/widgets/assetwidget.ui 
    src/widgets/assetpickerwidget.ui 
    src/widgets/keyframelabel.ui 
    src/widgets/keyframecurvewidget.ui 
    src/dialogs/getnamedialog.ui 
    src/widgets/createanimationwidget.ui 
    src/dialogs/progressdialog.ui 
    src/widgets/screenshotwidget.ui 
    src/widgets/projectmanager.ui 
    src/dialogs/renameprojectdialog.ui 
    src/dialogs/donate.ui
)

qt5_wrap_cpp(HEADERS_moc ${HEADERS})
qt5_wrap_ui(UI_moc ${FORMS})

# Todo - trim these down to nothing eventually
qt5_add_resources(QRCS
    ${CMAKE_SOURCE_DIR}/app/fonts.qrc
    ${CMAKE_SOURCE_DIR}/app/icons.qrc
    ${CMAKE_SOURCE_DIR}/app/images.qrc
    ${CMAKE_SOURCE_DIR}/app/models.qrc
    ${CMAKE_SOURCE_DIR}/app/shaders.qrc 
)

# Disable compiler specific warnings if we are building with MSVC
# NOMINMAX - http://www.suodenjoki.dk/us/archive/2010/min-max.htm
if(MSVC)
    add_definitions("-D_CRT_SECURE_NO_WARNINGS" "-DNOMINMAX")
    # Disable ZERO_CHECK if need be
    # set(CMAKE_SUPPRESS_REGENERATION true)
endif()

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL Release)
    set(CMAKE_WIN32_EXECUTABLE ON)
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL MSVC)
    set(DestDir ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE})
    # set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${CMAKE_PROJECT_NAME})
else()
    set(DestDir ${CMAKE_BINARY_DIR}/bin)
endif()

# For convenience, put the shared files built from IrisGL along with the final binary
# Also note MSVC build path is different from GNU in that it containts the build type
set_target_properties(IrisGL PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_target_properties(assimp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_target_properties(breakpad PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_target_properties(crash_handler PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(${CMAKE_PROJECT_NAME} src/main.cpp ${SRCS} ${HEADERS_moc} ${QRCS})
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
															src
															irisgl/src
															irisgl/src/assimp/include/
															irisgl/src/libovr/include/
															thirdparty/breakpad/breakpad/src)
target_link_libraries(${CMAKE_PROJECT_NAME} Qt5::Concurrent Qt5::Sql Qt5::Svg Qt5::Widgets IrisGL breakpad ${OPENGL_LIBRARIES})

include(CopyResources)
include(CopyDependencies)
