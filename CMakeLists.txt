cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)
foreach(p CMP0071) # 3.10: Let AUTOMOC and AUTOUIC process GENERATED files
    if(POLICY ${p})
        cmake_policy(SET ${p} NEW)
    endif()
endforeach()
project(Jahshaka)

set(APP_OUTPUT_NAME "Jahshaka")

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(OpenGL REQUIRED) # is this needed?
find_package(Qt6 REQUIRED COMPONENTS Concurrent Network Widgets Sql Svg Multimedia Qml)

include(UseModernCpp)
include(AddExternalSubdirectories)
include(BuildType)

set_property(GLOBAL PROPERTY USE_FOLDERS TRUE)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
# The remapped tree keeps .ui files beside their widgets; cross-directory
# ui_*.h includes (e.g. accordionbladewidget uses propertywidget's) need the
# search path to span every .ui directory.
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/panels/timeline
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/panels/presets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/panels/propertywidgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/controls
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs/preferences
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/preferences)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER AutoMocFolder)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/irisgl)

add_subdirectory_with_folder("Extras" ${CMAKE_CURRENT_SOURCE_DIR}/extras/downloader)

set(downloaderProject downloader)

set(SRCS
    src/main.cpp
    src/mainwindow.cpp 
    src/ui/controls/colorvaluewidget.cpp 
    src/ui/controls/colorpickerwidget.cpp 
    src/dialogs/preferencesdialog.cpp 
    src/dialogs/aboutdialog.cpp 
    src/dialogs/preferences/worldsettings.cpp 
    src/dialogs/preferences/worldsettingswidget.cpp 
	src/dialogs/customdialog.cpp
	src/dialogs/custompopup.cpp
    src/dialogs/loadmeshdialog.cpp 
    src/ui/panels/timeline/animationwidget.cpp 
    src/ui/panels/timeline/keyframelabelwidget.cpp 
    src/ui/panels/timeline/keyframewidget.cpp 
    src/ui/panels/timeline/maintimelinewidget.cpp 
    src/ui/panels/timeline/timelinewidget.cpp 
    src/core/nodekeyframeanimation.cpp 
    src/core/project.cpp 
    src/core/settingsmanager.cpp 
    src/editor/editorcameracontroller.cpp 
    src/dialogs/infodialog.cpp 
    src/globals.cpp 
    src/viewport/engineviewwidget.cpp
    src/engine/scenemirror.cpp
    src/viewport/enginesceneviewport.cpp
    src/editor/scenepicker.cpp
    src/editor/gizmooverlay.cpp
    src/viewport/enginerenderdriver.cpp
    src/dialogs/ogrepreviewdialog.cpp
    src/engine/enginehost.cpp
    src/scripting/apimodule.cpp
    src/scripting/apiregistry.cpp
    src/scripting/scriptengine.cpp
    src/scripting/scriptconsole.cpp
    src/scripting/modules/projectapi.cpp
    src/scripting/modules/sceneapi.cpp
    src/scripting/modules/nodeapi.cpp
    src/scripting/modules/editorapi.cpp
    src/scripting/modules/appapi.cpp
    src/scripting/modules/worldapi.cpp
    src/scripting/modules/assetsapi.cpp
    src/scripting/modules/materialsapi.cpp
    src/scripting/modules/studiomodules.cpp
    src/services/assetimporter.cpp
    src/services/assetservice.h
    src/services/projectservice.cpp
    src/services/sceneeditservice.cpp
    src/services/thumbnailservice.cpp
    src/services/undoservice.cpp
    src/services/selectionservice.cpp
    src/services/playbackservice.cpp
    src/ui/panels/presets/assetfavorites.cpp 
    src/ui/panels/presets/assetmodelpanel.cpp 
    src/ui/panels/presets/assetmaterialpanel.cpp 
    src/ui/controls/accordionbladewidget.cpp 
    src/ui/panels/transformeditor.cpp 
    src/ui/controls/dragspinbox.cpp 
    src/ui/panels/scenehierarchywidget.cpp 
    src/editor/cameracontrollerbase.cpp 
    src/editor/gizmo.cpp 
    src/editor/gizmomeshes.cpp 
    src/editor/translationgizmo.cpp 
    src/editor/scalegizmo.cpp 
    src/editor/rotationgizmo.cpp 
    src/ui/panels/presets/skypresets.cpp 
    src/editor/orbitalcameracontroller.cpp 
    src/ui/panels/scenenodepropertieswidget.cpp 
    src/ui/panels/propertywidgets/lightpropertywidget.cpp 
    src/ui/panels/propertywidgets/materialpropertywidget.cpp 
    src/ui/panels/propertywidgets/skypropertywidget.cpp 
    src/ui/panels/propertywidgets/worldskypropertywidget.cpp
    src/ui/panels/propertywidgets/worldgipropertywidget.cpp
    src/ui/panels/propertywidgets/worldpropertywidget.cpp 
    src/ui/panels/propertywidgets/physicspropertywidget.cpp 
    src/ui/panels/propertywidgets/cubemapwidget.cpp 
    src/io/scenewriter.cpp 
    src/core/thumbnailmanager.cpp 
    src/ui/panels/propertywidgets/fogpropertywidget.cpp 
    src/io/assetiobase.cpp 
    src/io/materialpresetreader.cpp 
    src/ui/panels/timeline/keyframelabeltreewidget.cpp 
    src/core/keyboardstate.cpp 
    src/io/scenereader.cpp 
    src/ui/panels/propertywidgets/emitterpropertywidget.cpp 
    src/ui/panels/propertywidgets/nodepropertywidget.cpp
    src/ui/controls/labelwidget.cpp
    src/ui/controls/checkboxwidget.cpp 
    src/ui/controls/textinputwidget.cpp 
    src/ui/controls/comboboxwidget.cpp 
    src/ui/controls/hfloatsliderwidget.cpp 
    src/ui/controls/texturepickerwidget.cpp 
    src/io/materialreader.cpp 
    src/ui/controls/filepickerwidget.cpp 
    src/ui/panels/propertywidgets/meshpropertywidget.cpp
    src/ui/panels/propertywidgets/shaderpropertywidget.cpp 
    src/ui/panels/propertywidget.cpp 
    src/ui/panels/propertywidgets/scenepropertywidget.cpp
    src/dialogs/newprojectdialog.cpp 
    src/ui/panels/assetwidget.cpp 
    src/io/assetmanager.cpp 
    src/ui/controls/assetpickerwidget.cpp 
    src/ui/panels/timeline/keyframelabel.cpp 
    src/uimanager.cpp 
    src/ui/panels/timeline/keyframecurvewidget.cpp 
    src/dialogs/getnamedialog.cpp 
    src/ui/panels/timeline/createanimationwidget.cpp 
    src/core/database/database.cpp 
    src/core/database/projectdatabase.cpp 
    src/core/guidmanager.cpp 
    src/commands/transfrormscenenodecommand.cpp 
    src/commands/changematerialpropertycommand.cpp 
    src/commands/addscenenodecommand.cpp
    src/commands/deletescenenodecommand.cpp
    src/commands/reparentscenenodecommand.cpp
    src/dialogs/progressdialog.cpp 
    src/ui/dialogs/screenshotwidget.cpp 
    src/ui/pages/projectmanager.cpp 
    src/ui/controls/dynamicgrid.cpp 
    src/ui/controls/itemgridwidget.cpp 
    src/dialogs/renameprojectdialog.cpp 
    src/editor/thumbnailgenerator.cpp 
    src/editor/enginethumbnailrenderer.cpp 
    src/dialogs/donatedialog.cpp
    src/ui/pages/assetview.cpp
    src/ui/pages/assetview.cpp
    src/bridge/engineassetscene.cpp
    src/ui/pages/engineassetviewer.cpp
    src/bridge/enginematerialpreviewscene.cpp
    src/bridge/enginematerialpreview.cpp
    src/ui/controls/assetviewgrid.cpp
    src/ui/controls/assetgriditem.cpp
    src/constants.cpp
    src/misc/updatechecker.cpp
    src/dialogs/softwareupdatedialog.cpp
    src/core/assethelper.cpp
    src/core/subscriber.cpp
    src/core/scenenodehelper.cpp
    src/misc/upgrader.cpp
    src/misc/QtAwesome.cpp
    src/misc/QtAwesomeAnim.cpp
    src/misc/stylesheet.cpp
    src/ui/controls/colorview.cpp
    src/dialogs/toast.cpp
    src/helpers/tooltip.cpp
    src/helpers/dialoghelper.cpp
    src/subclass/switch.cpp
    # player
    src/player/playerwidget.cpp
    src/player/engineplayerscene.cpp
    src/player/engineplayerview.cpp
    src/player/playermousecontroller.cpp
    src/player/playback.cpp
)

set(HEADERS
    src/mainwindow.h 
    src/ui/controls/colorvaluewidget.h 
    src/ui/controls/colorpickerwidget.h 
    src/helpers/settingshelper.h 
    src/dialogs/preferencesdialog.h 
    src/dialogs/aboutdialog.h 
    src/dialogs/preferences/worldsettings.h 
    src/dialogs/preferences/worldsettingswidget.h 
    src/dialogs/customdialog.h
    src/dialogs/custompopup.h
    src/helpers/collisionhelper.h 
    src/dialogs/infodialog.h 
    src/dialogs/loadmeshdialog.h 
    src/ui/panels/timeline/animationwidget.h 
    src/ui/panels/timeline/keyframelabelwidget.h 
    src/ui/panels/timeline/keyframewidget.h
    src/ui/panels/timeline/maintimelinewidget.h
    src/ui/panels/timeline/timelinewidget.h 
    src/core/keyframeanimation.h 
    src/core/keyframes.h 
    src/core/nodekeyframe.h 
    src/core/nodekeyframeanimation.h 
    src/core/project.h 
    src/core/settingsmanager.h 
    src/editor/editorcameracontroller.h 
    src/globals.h 
    src/viewport/engineviewwidget.h
    src/engine/scenemirror.h
    src/viewport/enginesceneviewport.h
    src/editor/headlesseditorviewport.h
    src/editor/ieditorviewport.h
    src/editor/scenepicker.h
    src/editor/gizmooverlay.h
    src/viewport/enginerenderdriver.h
    src/dialogs/ogrepreviewdialog.h
    src/engine/enginehost.h
    src/scripting/scripthost.h
    src/scripting/apimodule.h
    src/scripting/apiregistry.h
    src/scripting/scriptengine.h
    src/scripting/scriptconsole.h
    src/scripting/modules/moduleshared.h
    src/scripting/modules/projectapi.h
    src/scripting/modules/sceneapi.h
    src/scripting/modules/nodeapi.h
    src/scripting/modules/editorapi.h
    src/scripting/modules/appapi.h
    src/scripting/modules/worldapi.h
    src/scripting/modules/assetsapi.h
    src/scripting/modules/materialsapi.h
    src/scripting/modules/studiomodules.h
    src/services/services.h
    src/services/assetimporter.h
    src/services/projectservice.h
    src/services/sceneeditservice.h
    src/services/thumbnailservice.h
    src/services/undoservice.h
    src/services/selectionservice.h
    src/services/playbackservice.h
    src/ui/panels/presets/assetfavorites.h 
    src/ui/panels/presets/assetpanel.h 
    src/ui/panels/presets/assetmodelpanel.h 
    src/ui/panels/presets/assetmaterialpanel.h 
    src/ui/controls/accordionbladewidget.h 
    src/ui/panels/transformeditor.h 
    src/ui/controls/dragspinbox.h 
    src/ui/panels/scenehierarchywidget.h 
    src/editor/orbitalcameracontroller.h 
    src/editor/cameracontrollerbase.h 
    src/ui/panels/presets/skypresets.h 
    src/ui/panels/propertywidgets/transformpropertywidget.h 
    src/ui/panels/propertywidgets/lightpropertywidget.h 
    src/ui/panels/propertywidgets/materialpropertywidget.h 
    src/ui/panels/propertywidgets/worldskypropertywidget.h
    src/ui/panels/propertywidgets/worldgipropertywidget.h 
    src/ui/panels/propertywidgets/skypropertywidget.h 
    src/ui/panels/scenenodepropertieswidget.h 
    src/ui/panels/propertywidgets/worldpropertywidget.h 
    src/ui/panels/propertywidgets/physicspropertywidget.h 
    src/ui/panels/propertywidgets/cubemapwidget.h
    src/io/scenewriter.h 
    src/io/scenereader.h 
    src/ui/panels/propertywidgets/scenepropertywidget.h 
    src/core/thumbnailmanager.h 
    src/ui/panels/propertywidgets/fogpropertywidget.h 
    src/core/meshmanager.h 
    src/io/assetiobase.h 
    src/core/materialpreset.h 
    src/io/materialpresetreader.h 
    src/ui/panels/timeline/keyframelabeltreewidget.h 
    src/io/materialpresetreader.h 
    src/editor/gizmo.h 
    src/editor/gizmomeshes.h 
    src/editor/translationgizmo.h 
    src/editor/rotationgizmo.h 
    src/editor/scalegizmo.h 
    src/core/keyboardstate.h 
    src/editor/editordata.h 
    src/ui/panels/propertywidgets/emitterpropertywidget.h 
    src/ui/panels/propertywidgets/nodepropertywidget.h
    src/ui/controls/labelwidget.h
    src/ui/controls/checkboxwidget.h 
    src/ui/controls/textinputwidget.h 
    src/ui/controls/comboboxwidget.h 
    src/ui/controls/hfloatsliderwidget.h 
    src/ui/controls/texturepickerwidget.h 
    src/io/materialreader.hpp 
    src/ui/controls/filepickerwidget.h 
    src/ui/panels/propertywidgets/meshpropertywidget.h 
    src/ui/panels/propertywidgets/shaderpropertywidget.h 
    src/ui/panels/propertywidget.h 
    src/ui/controls/basewidget.h 
    src/dialogs/newprojectdialog.h 
    src/ui/panels/assetwidget.h 
    src/io/assetmanager.h 
    src/ui/controls/assetpickerwidget.h 
    src/constants.h 
    src/ui/panels/timeline/keyframelabel.h 
    src/uimanager.h 
    src/ui/panels/timeline/keyframecurvewidget.h 
    src/ui/panels/timeline/animationwidgetdata.h 
    src/dialogs/getnamedialog.h 
    src/ui/panels/timeline/createanimationwidget.h 
    src/core/database/database.h 
    src/core/database/projectdatabase.h 
    src/core/guidmanager.h 
    src/commands/transfrormscenenodecommand.h 
    src/commands/changematerialpropertycommand.h 
    src/commands/addscenenodecommand.h
    src/commands/deletescenenodecommand.h
    src/commands/reparentscenenodecommand.h
    src/dialogs/progressdialog.h 
    src/commands/deleteanimationkeycommand.h 
    src/ui/dialogs/screenshotwidget.h 
    src/ui/pages/projectmanager.h 
    src/ui/controls/dynamicgrid.h 
    src/ui/controls/itemgridwidget.h 
    src/dialogs/renameprojectdialog.h 
    src/editor/thumbnailgenerator.h 
    src/dialogs/donatedialog.h
    src/ui/pages/assetview.h
    src/ui/pages/iassetviewer.h
    src/ui/pages/headlessassetviewer.h
    src/bridge/engineassetscene.h
    src/ui/pages/engineassetviewer.h
    src/bridge/enginematerialpreviewscene.h
    src/bridge/enginematerialpreview.h
    src/ui/controls/assetviewgrid.h
    src/ui/controls/assetgriditem.h
    src/breakpad/breakpad.h
    src/misc/updatechecker.h
    src/dialogs/softwareupdatedialog.h
    src/core/assethelper.h
    src/core/subscriber.h
    src/core/scenenodehelper.h
    src/misc/upgrader.h
    src/misc/QtAwesome.h
    src/misc/QtAwesomeAnim.h 
    src/misc/stylesheet.h
    src/ui/controls/colorview.h
    src/dialogs/toast.h
    src/helpers/tooltip.h
    src/helpers/dialoghelper.h

    # player
    src/subclass/switch.h
    src/player/playerwidget.h
    src/player/engineplayerscene.h
    src/player/engineplayerview.h
    src/player/sceneplayer.h
    src/player/playermousecontroller.h
    src/player/playback.h
)

# Todo - trim these down to nothing eventually
set(WIDGETS
    src/mainwindow.ui 
    src/ui/controls/colorvaluewidget.ui 
    src/ui/controls/colorpickerwidget.ui 
    src/dialogs/preferencesdialog.ui 
    src/dialogs/aboutdialog.ui 
    src/dialogs/preferences/worldsettings.ui 
    src/dialogs/infodialog.ui 
    src/dialogs/loadmeshdialog.ui 
    src/ui/panels/timeline/animationwidget.ui 
    src/ui/panels/timeline/maintimelinewidget.ui 
    src/ui/controls/accordionbladewidget.ui 
    src/ui/panels/scenehierarchywidget.ui 
    src/ui/panels/presets/skypresets.ui 
    src/ui/panels/timeline/keyframelabelwidget.ui 
    src/ui/panels/timeline/keyframelabeltreewidget.ui 
    src/ui/controls/labelwidget.ui 
    src/ui/controls/checkboxwidget.ui 
    src/ui/controls/textinputwidget.ui 
    src/ui/controls/comboboxwidget.ui 
    src/ui/controls/hfloatsliderwidget.ui 
    src/ui/controls/texturepickerwidget.ui 
    src/ui/controls/filepickerwidget.ui 
    src/ui/panels/propertywidget.ui 
    src/dialogs/newprojectdialog.ui 
    src/ui/panels/assetwidget.ui 
    src/ui/controls/assetpickerwidget.ui 
    src/ui/panels/timeline/keyframelabel.ui 
    src/ui/panels/timeline/keyframecurvewidget.ui 
    src/dialogs/getnamedialog.ui 
    src/ui/panels/timeline/createanimationwidget.ui 
    src/dialogs/progressdialog.ui 
    src/ui/dialogs/screenshotwidget.ui 
    src/ui/pages/projectmanager.ui 
    src/dialogs/renameprojectdialog.ui 
    src/dialogs/donate.ui
    src/dialogs/softwareupdatedialog.ui
)

include(IncludeOgre)
# The engine abstraction lives in the IrisGL repo (owner decision 2026-08-30:
# Studio = the app; IrisGL = document + engine). It stays a separate library —
# the document never links it — and keeps build-linux/engine as its binary dir
# so test and tooling paths are unchanged.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/irisgl/engine ${CMAKE_CURRENT_BINARY_DIR}/engine)

# Headless engine tests (tests/). include(CTest) defines BUILD_TESTING and calls
# enable_testing() at top level, which add_test in a subdirectory requires.
# Which editor viewport starts by default. Overridable at runtime with
# --viewport=engine|legacy or env JAHSHAKA_VIEWPORT (see src/engine/enginehost.h).
option(JAHSHAKA_ENGINE_VIEWPORT "Default to the engine viewport" OFF)

option(JAHSHAKA_BUILD_TESTS "Build the headless engine test suite" ON)
if(JAHSHAKA_BUILD_TESTS)
    include(CTest)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

set(LIBS Qt6::Concurrent Qt6::Sql Qt6::Svg Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::Qml IrisGL JahshakaEngine ${OPENGL_LIBRARIES})

include(IncludeBreakpad)
include(IncludeShaderGraph)

set (QRCS
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

set(APP_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${APP_OUTPUT_DIR})
set(DestDir ${APP_OUTPUT_DIR})

# git commit hash macro
execute_process(
    COMMAND git log -1 --pretty=format:%h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_definitions("-DGIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")

execute_process(
    COMMAND git log -1 --pretty=format:%cd
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_definitions("-DGIT_COMMIT_DATE=\"${GIT_COMMIT_DATE}\"")

# For convenience, put the shared files built from IrisGL along with the final binary
# Also note MSVC build path is different from GNU in that it containts the build type
set_target_properties(IrisGL PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_target_properties(assimp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_target_properties(downloader PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# -----------------------------
# Linux: put executables and shared libraries into bin/
# -----------------------------
if(UNIX)
    # Executables
    set_target_properties(downloader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Shared libraries (.so)
    set_target_properties(IrisGL assimp PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# osx icon
# https://stackoverflow.com/questions/22745728/cmake-set-icon-for-a-mac-osx-app

set(MACOSX_BUNDLE_ICON_FILE icon.icns)
set(BUNDLE_ICON ${CMAKE_CURRENT_SOURCE_DIR}/app/icons/icon.icns)

set_source_files_properties(${BUNDLE_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

# https://stackoverflow.com/a/37211780/996468
source_group("Resources" FILES ${QRCS} ${WIDGETS} ${BUNDLE_ICON})

add_executable(${CMAKE_PROJECT_NAME} MACOSX_BUNDLE src/main.cpp ${SRCS} ${HEADERS} ${QRCS} ${WIDGETS} app/winapp.rc ${BUNDLE_ICON}
    src/app/versionsplashscreen.h src/app/versionsplashscreen.cpp)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${APP_OUTPUT_NAME})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${CMAKE_PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
                            src
                            irisgl/include
                            irisgl
                            thirdparty/breakpad/breakpad/src
                            downloader)

#force build downloader
#luckily, the jahshaka project has all the dependencies of the downloader project
#so we dont need to run windeployqt or macdeployqt on the downloader specifically
add_dependencies(${CMAKE_PROJECT_NAME} ${downloaderProject})

if(USE_BREAKPAD)
    set(LIBS ${LIBS} breakpad)
endif(USE_BREAKPAD)

target_link_libraries(${CMAKE_PROJECT_NAME} ${LIBS})
if(JAHSHAKA_ENGINE_VIEWPORT)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE JAHSHAKA_ENGINE_VIEWPORT_DEFAULT=1)
else()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE JAHSHAKA_ENGINE_VIEWPORT_DEFAULT=0)
endif()

# A6: these two warning classes are undefined behaviour, not style - keep them fixed.
if (NOT MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Werror=return-type -Werror=delete-incomplete)
endif()

include(CopyResources)
include(CopyDependencies)

if((WIN32 OR APPLE OR UNIX) AND CMAKE_BUILD_TYPE STREQUAL "Release")
    string(TIMESTAMP CURRENT_DATE "%Y-%m-%d")
    set(RELEASE_DATE "${CURRENT_DATE}")
    set(CMAKE_PROJECT_VERSION "0.9.0")
    include(${CMAKE_SOURCE_DIR}/deploy/installer/config.cmake)
endif()
