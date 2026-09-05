cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)
foreach(p CMP0071) # 3.10: Let AUTOMOC and AUTOUIC process GENERATED files
    if(POLICY ${p})
        cmake_policy(SET ${p} NEW)
    endif()
endforeach()

# macOS deployment floor. MUST be set before project() — CMake stamps it into
# every Mach-O's LC_BUILD_VERSION at first compiler test. Left unset, the
# toolchain uses the *SDK's* version (minos 26.0 on an Xcode 17 box), and the
# redistributable bundle then refuses to launch on anything older, for no
# reason of ours. 13.0 is the Qt 6.11.2 kit's own floor (MoltenVK ships 12.0),
# so it is the lowest we can honestly claim; irisgl/scripts/build-ogre.sh
# passes the same value to Ogre-Next. Non-APPLE builds never see this.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target")
endif()

project(Jahshaka)

set(APP_OUTPUT_NAME "Jahshaka")

# Bundle identity (MACOS_BUNDLE_SPEC §1.8): CMake's MacOSXBundleInfo.plist.in
# template fills CFBundleIdentifier/Name/Version from these. Left unset they
# expand to empty strings and codesign falls back to Jahshaka-<hash> with
# "Info.plist=not bound". The packaging script rewrites the plist wholesale,
# but the built bundle should be correct on its own.
set(JAHSHAKA_VERSION "0.9.0")
set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.jahshaka.studio")
set(MACOSX_BUNDLE_BUNDLE_NAME "Jahshaka")
set(MACOSX_BUNDLE_EXECUTABLE_NAME "${APP_OUTPUT_NAME}")
set(MACOSX_BUNDLE_BUNDLE_VERSION "${JAHSHAKA_VERSION}")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${JAHSHAKA_VERSION}")
set(MACOSX_BUNDLE_LONG_VERSION_STRING "${JAHSHAKA_VERSION}")
set(MACOSX_BUNDLE_INFO_STRING "Jahshaka Studio")
set(MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2016-2026 EXEDOS LLC")

# RPATH, not RUNPATH, on Linux.
#
# Modern binutils emits DT_RUNPATH, which the loader applies ONLY to a binary's
# OWN DT_NEEDED entries — it is not inherited by their dependencies. Every rpath
# we set points at the Ogre install prefix, and Ogre's own libraries carry no
# rpath at all, so the moment one Ogre component depends on another that is not
# in the default search path the load fails with a misleading "cannot open
# shared object file" naming the INNER library.
#
# That is not hypothetical: enabling the PlanarReflections component made
# libOgreNextHlmsPbs depend on libOgreNextPlanarReflections, and every binary in
# the tree stopped loading. DT_RPATH is inherited, so one flag fixes the whole
# family and every future Ogre component. (The real fix belongs upstream of us —
# build-ogre.sh should install Ogre with CMAKE_INSTALL_RPATH=$ORIGIN, the way its
# macOS branch already bakes @loader_path — but this makes us robust either way.)
if(UNIX AND NOT APPLE)
    add_link_options(-Wl,--disable-new-dtags)

    # -rdynamic: readable crash logs (STABILITY_PROGRAM_SPEC Lane 7).
    #
    # crashhandler.cpp writes its backtrace with backtrace_symbols_fd(), which
    # resolves addresses through dladdr() — and dladdr() can only see an
    # executable's DYNAMIC symbol table. Without this flag a PIE executable
    # exports nothing, so every frame of every crash-*.log printed as
    # `./Jahshaka(+0x1234ab) [0x5566...]`: module plus offset, no names.
    # (Verified before the change: `readelf --dyn-syms bin/Jahshaka |
    # grep -c MainWindow` == 0.)
    #
    # -rdynamic puts every extern-linkage symbol in .dynsym, so the frames now
    # read `./Jahshaka(_ZN10MainWindow...+0x2a)`. It does NOT help statics or
    # anonymous-namespace functions — those never reach .dynsym — which is why
    # scripts/debug-crash.sh still runs addr2line over the raw offsets; DWARF
    # resolves those and gives file:line as well. Keep both.
    #
    # Cost: a bigger .dynsym and slightly slower symbol lookup at load. Nothing
    # about linking or the loader's search order changes.
    add_link_options(-rdynamic)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ThreadSanitizer: the race lane (deep audit 2026-09, area 3 finding 4).
#
# TSan is all-or-nothing in a way ASan is not — it only sees the memory accesses
# and the synchronisation of code it instrumented, so a partial build produces
# FALSE races (a happens-before edge established inside an uninstrumented
# library is invisible to it). Hence a global flag rather than the per-target
# opt-in JAHSHAKA_ASAN uses, and hence its own build directory: the two runtimes
# cannot coexist in one process, and every TU has to be recompiled anyway.
#
#     cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DJAHSHAKA_TSAN=ON ...
#     ./scripts/sanitize.sh tsan
#
# Ogre-Next, Qt and the Vulkan driver stay uninstrumented (we link installed
# shared libraries) — scripts/tsan.supp documents what that costs. Placed before
# add_subdirectory(irisgl) so the flags reach every target in the tree.
option(JAHSHAKA_TSAN "Build the whole tree with ThreadSanitizer (use a separate build dir)" OFF)
if(JAHSHAKA_TSAN)
    if(JAHSHAKA_ASAN)
        message(FATAL_ERROR
            "JAHSHAKA_TSAN and JAHSHAKA_ASAN cannot be combined — the two runtimes "
            "refuse to load into one process. Configure a separate build directory.")
    endif()
    if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
        message(FATAL_ERROR "JAHSHAKA_TSAN needs GCC or Clang")
    endif()
    # -g so the reports carry file:line; frame pointers so the stacks are whole.
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
    message(STATUS "ThreadSanitizer: ON (whole tree; Ogre/Qt remain uninstrumented)")
endif()

find_package(OpenGL REQUIRED) # is this needed?
find_package(Qt6 REQUIRED COMPONENTS Concurrent Network Widgets Sql Svg Multimedia Qml HttpServer)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs/preferences)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER AutoMocFolder)

# Ogre-Next must be resolved BEFORE irisgl is added: the document library now
# owns the scene graph (SPECS/SCENEGRAPH_SPEC.md D2) and its one Ogre-private
# translation unit — irisgl/document/scenegraph/nodegraph.cpp — links OgreNext.
# Nothing else changes: this file only defines the OgreNext INTERFACE target and
# the media/plugin paths.
include(IncludeOgre)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/irisgl)

add_subdirectory_with_folder("Extras" ${CMAKE_CURRENT_SOURCE_DIR}/extras/downloader)

set(downloaderProject downloader)

set(SRCS
    src/app/main.cpp
    src/app/crashhandler.cpp
    src/app/cli/clioptions.cpp
    src/app/cli/scriptrunner.cpp
    src/app/cli/selftestrunner.cpp
    src/shell/mainwindow.cpp 
    src/ui/controls/colorvaluewidget.cpp 
    src/ui/controls/colorpickerwidget.cpp 
    src/ui/dialogs/preferencesdialog.cpp 
    src/ui/dialogs/aboutdialog.cpp 
    src/ui/dialogs/preferences/worldsettingswidget.cpp 
	src/ui/dialogs/customdialog.cpp
	src/ui/dialogs/custompopup.cpp
    src/ui/dialogs/loadmeshdialog.cpp 
    src/ui/panels/timeline/animationwidget.cpp 
    src/ui/panels/timeline/keyframewidget.cpp 
    src/ui/panels/timeline/timelinewidget.cpp 
    src/ui/panels/timeline/nodekeyframeanimation.cpp 
    src/data/project.cpp 
    src/data/settingsmanager.cpp 
    src/viewport/editorcameracontroller.cpp 
    src/ui/dialogs/infodialog.cpp 
    src/viewport/engineviewwidget.cpp
    irisgl/mirror/scenemirror.cpp
    src/viewport/enginesceneviewport.cpp
    src/viewport/scenepicker.cpp
    src/viewport/gizmooverlay.cpp
    src/viewport/enginerenderdriver.cpp
    src/ui/dialogs/ogrepreviewdialog.cpp
    src/bridge/enginehost.cpp
    src/scripting/apimodule.cpp
    src/scripting/apiregistry.cpp
    src/scripting/scriptengine.cpp
    src/ui/panels/scriptconsole.cpp
    src/scripting/modules/projectapi.cpp
    src/scripting/modules/sceneapi.cpp
    src/scripting/modules/nodeapi.cpp
    src/scripting/modules/editorapi.cpp
    src/scripting/modules/appapi.cpp
    src/scripting/modules/desktopapi.cpp
    src/scripting/modules/worldapi.cpp
    src/scripting/modules/assetsapi.cpp
    src/scripting/modules/particlesapi.cpp
    src/scripting/modules/animapi.cpp
    src/scripting/modules/cameraapi.cpp
    src/modules/materials/api/materialsapi.cpp
    src/modules/materials/materialsmodule.cpp
    src/modules/publish/publishmodule.cpp
    src/modules/publish/publishrecord.cpp
    src/modules/avatar/avatarmodule.cpp
    src/modules/avatar/avatarpage.cpp
    src/modules/avatar/avatarpreviewmodel.cpp
    src/modules/avatar/avatarspace.cpp
    src/modules/avatar/avatarsockets.cpp
    src/modules/avatar/api/avatarapi.cpp
    src/bridge/avatarpreviewscene.cpp
    src/bridge/avatarpreview.cpp
    src/viewport/boneoverlay.cpp
    src/export/gltfexporter.cpp
    src/export/exportservice.cpp
    src/export/previewlauncher.cpp
    src/export/embeddedpreview.cpp
    src/export/walkers/scenewalker.cpp
    src/export/walkers/meshbufferreader.cpp
    src/export/walkers/materialtexturereader.cpp
    src/export/exportmanifest.cpp
    src/export/exportcontentsource.cpp
    src/export/rawexporter.cpp
    src/scripting/modules/studiomodules.cpp
    src/scripting/mcp/mcpserver.cpp
    src/scripting/mcp/mcptools.cpp
    src/scripting/claude/claudecliprobe.cpp
    src/scripting/claude/claudelaunchconfig.cpp
    src/scripting/claude/claudestreamparser.cpp
    src/scripting/claude/claudechathost.cpp
    src/ui/windows/claudechatwindow.cpp
    src/ui/dialogs/preferences/mcpsettingswidget.cpp
    src/ui/dialogs/preferences/cachesettingswidget.cpp
    src/ui/dialogs/preferences/assetssettingswidget.cpp
    src/services/assetimporter.cpp
    src/services/import/assetimportservice.cpp
    src/services/import/assetimporters.cpp
    src/services/import/importbatchrunner.cpp
    src/services/projectassets.cpp
    src/services/engineerrorpump.cpp
    src/services/loadtimeline.cpp
    src/services/meshbakestore.cpp
    src/services/mainthreadheartbeat.cpp
    src/services/mainthreadwatchdog.cpp
    src/shell/shutdownorder.cpp
    src/services/sceneopenrunner.cpp
    src/services/imagematerial.cpp
    src/services/projectarchiver.cpp
    src/io/ziphelper.cpp
    src/services/assetmetadata.cpp
    src/services/iesprofile.cpp
    src/services/lightbindings.cpp
    src/services/assetcas.cpp
    src/services/assetgc.cpp
    src/services/assetmigration.cpp
    src/services/assetstore.cpp
    src/services/assetstorepaths.cpp
    src/services/audiopeaks.cpp
    src/services/videoutils.cpp
    src/services/assetservice.h
    src/services/projectservice.cpp
    src/services/sceneeditservice.cpp
    src/services/thumbnailservice.cpp
    src/services/undoservice.cpp
    src/services/selectionservice.cpp
    src/services/shortcutregistry.cpp
    src/services/worldmodes.cpp
    src/services/planarreflectors.cpp
    src/services/animationedits.cpp
    src/services/playbackservice.cpp
    src/ui/panels/presets/assetmodelpanel.cpp 
    src/ui/panels/presets/assetmaterialpanel.cpp 
    src/ui/controls/accordionbladewidget.cpp 
    src/ui/panels/transformeditor.cpp 
    src/ui/controls/dragspinbox.cpp 
    src/ui/panels/scenehierarchywidget.cpp 
    src/viewport/cameracontrollerbase.cpp 
    src/viewport/gizmo.cpp
    src/viewport/snapsettings.cpp
    src/viewport/gizmomeshes.cpp 
    src/viewport/translationgizmo.cpp 
    src/viewport/scalegizmo.cpp 
    src/viewport/rotationgizmo.cpp 
    src/ui/panels/presets/skypresets.cpp 
    src/viewport/orbitalcameracontroller.cpp 
    src/ui/panels/scenenodepropertieswidget.cpp 
    src/ui/panels/propertywidgets/lightpropertywidget.cpp 
    src/ui/panels/propertywidgets/decalpropertywidget.cpp 
    src/ui/panels/propertywidgets/materialpropertywidget.cpp 
    src/ui/panels/propertywidgets/skypropertywidget.cpp 
    src/ui/panels/propertywidgets/worldskypropertywidget.cpp
    src/ui/panels/propertywidgets/worldgipropertywidget.cpp
    src/ui/panels/propertywidgets/worldaapropertywidget.cpp
    src/ui/panels/propertywidgets/worldmodespropertywidget.cpp
    src/ui/panels/propertywidgets/worldshadowpropertywidget.cpp
    src/ui/panels/propertywidgets/worldpropertywidget.cpp 
    src/ui/panels/propertywidgets/physicspropertywidget.cpp 
    src/ui/panels/propertywidgets/cubemapwidget.cpp 
    src/io/scenewriter.cpp 
    src/services/thumbnailmanager.cpp 
    src/ui/panels/propertywidgets/fogpropertywidget.cpp 
    src/io/assetiobase.cpp 
    src/io/materialpresetreader.cpp 
    src/ui/panels/timeline/keyframelabeltreewidget.cpp 
    src/viewport/keyboardstate.cpp 
    src/io/scenereader.cpp 
    src/ui/panels/propertywidgets/emitterpropertywidget.cpp 
    src/ui/panels/propertywidgets/nodepropertywidget.cpp
    src/ui/controls/labelwidget.cpp
    src/ui/controls/checkboxwidget.cpp 
    src/ui/controls/textinputwidget.cpp 
    src/ui/controls/comboboxwidget.cpp 
    src/ui/controls/hfloatsliderwidget.cpp 
    src/ui/controls/particlerampwidget.cpp
    src/ui/controls/texturepickerwidget.cpp 
    src/io/materialreader.cpp 
    src/ui/controls/filepickerwidget.cpp 
    src/ui/panels/propertywidgets/meshpropertywidget.cpp
    src/ui/panels/propertywidgets/shaderpropertywidget.cpp 
    src/ui/panels/propertywidget.cpp 
    src/ui/panels/propertywidgets/scenepropertywidget.cpp
    src/ui/dialogs/newprojectdialog.cpp 
    src/ui/panels/assetwidget.cpp 
    src/io/assetmanager.cpp 
    src/ui/controls/assetpickerwidget.cpp
    src/ui/controls/libraryassetpicker.cpp 
    src/ui/panels/timeline/keyframelabel.cpp 
    src/ui/panels/timeline/keyframecurvewidget.cpp 
    src/ui/dialogs/getnamedialog.cpp 
    src/ui/panels/timeline/createanimationwidget.cpp 
    src/data/database/database.cpp 
    src/data/database/projectdatabase.cpp 
    src/data/guidmanager.cpp 
    src/commands/transformscenenodecommand.cpp 
    src/commands/changematerialpropertycommand.cpp
    src/commands/changematerialcommand.cpp
    src/commands/addscenenodecommand.cpp
    src/commands/deletescenenodecommand.cpp
    src/commands/reparentscenenodecommand.cpp
    src/commands/scenefoldercommand.cpp
    src/commands/setnodepropertycommand.cpp
    src/commands/nodeeditcommand.cpp
    src/ui/dialogs/progressdialog.cpp 
    src/ui/dialogs/screenshotwidget.cpp 
    src/ui/pages/projectmanager.cpp 
    src/ui/controls/dynamicgrid.cpp
    src/ui/controls/itemgridwidget.cpp
    src/ui/controls/sliderlayoutmodel.cpp
    src/ui/dialogs/renameprojectdialog.cpp 
    src/services/thumbnailgenerator.cpp 
    src/bridge/enginethumbnailrenderer.cpp 
    src/ui/dialogs/donatedialog.cpp
    src/ui/pages/assetview.cpp
    src/ui/pages/importviewertail.cpp
    src/bridge/engineassetscene.cpp
    src/ui/pages/engineassetviewer.cpp
    src/bridge/enginematerialpreviewscene.cpp
    src/bridge/enginematerialpreview.cpp
    src/ui/controls/assetviewgrid.cpp
    src/ui/controls/assetgriditem.cpp
    src/ui/controls/drawertreewidget.cpp
    src/ui/controls/videopreviewwidget.cpp
    src/ui/controls/waveformwidget.cpp
    src/data/constants.cpp
    src/app/updatechecker.cpp
    src/ui/dialogs/softwareupdatedialog.cpp
    src/services/assethelper.cpp
    src/services/subscriber.cpp
    src/services/scenenodehelper.cpp
    src/app/upgrader.cpp
    thirdparty/qtawesome/QtAwesome.cpp
    thirdparty/qtawesome/QtAwesomeAnim.cpp
    src/ui/style/stylesheet.cpp
    src/ui/style/thememanager.cpp
    src/ui/controls/colorview.cpp
    src/ui/dialogs/toast.cpp
    src/ui/controls/tooltip.cpp
    src/ui/dialogs/dialoghelper.cpp
    # player
    src/player/playerwidget.cpp
    src/player/engineplayerscene.cpp
    src/player/engineplayerview.cpp
    src/player/playermousecontroller.cpp
    src/player/playback.cpp
)

set(HEADERS
    src/shell/mainwindow.h 
    src/ui/controls/colorvaluewidget.h 
    src/ui/controls/colorpickerwidget.h 
    src/ui/dialogs/preferencesdialog.h 
    src/ui/dialogs/aboutdialog.h 
    src/ui/dialogs/preferences/worldsettingswidget.h 
    src/ui/dialogs/customdialog.h
    src/ui/dialogs/custompopup.h
    src/services/collisionhelper.h 
    src/ui/dialogs/infodialog.h 
    src/ui/dialogs/loadmeshdialog.h 
    src/ui/panels/timeline/animationwidget.h 
    src/ui/panels/timeline/keyframewidget.h
    src/ui/panels/timeline/timelinewidget.h 
    src/ui/panels/timeline/timelinekeys.h 
    src/ui/panels/timeline/nodekeyframe.h 
    src/ui/panels/timeline/nodekeyframeanimation.h 
    src/data/project.h 
    src/data/settingsmanager.h 
    src/viewport/editorcameracontroller.h 
    src/viewport/engineviewwidget.h
    irisgl/mirror/scenemirror.h
    src/viewport/enginesceneviewport.h
    src/viewport/headlesseditorviewport.h
    src/viewport/ieditorviewport.h
    src/viewport/scenepicker.h
    src/viewport/gizmooverlay.h
    src/viewport/enginerenderdriver.h
    src/ui/dialogs/ogrepreviewdialog.h
    src/bridge/enginehost.h
    src/scripting/scripthost.h
    src/scripting/apimodule.h
    src/scripting/apiregistry.h
    src/scripting/scriptengine.h
    src/ui/panels/scriptconsole.h
    src/scripting/modules/moduleshared.h
    src/scripting/modules/projectapi.h
    src/scripting/modules/sceneapi.h
    src/scripting/modules/nodeapi.h
    src/scripting/modules/editorapi.h
    src/scripting/modules/appapi.h
    src/scripting/modules/desktopapi.h
    src/scripting/modules/worldapi.h
    src/scripting/modules/particlesapi.h
    src/scripting/modules/animapi.h
    src/scripting/modules/cameraapi.h
    src/scripting/modules/assetsapi.h
    src/modules/materials/api/materialsapi.h
    src/modules/materials/materialsmodule.h
    src/modules/publish/publishmodule.h
    src/modules/publish/publishrecord.h
    src/modules/avatar/avatarmodule.h
    src/modules/avatar/avatarpage.h
    src/modules/avatar/avatarpreviewmodel.h
    src/modules/avatar/avatarpreviewwidget.h
    src/modules/avatar/avatarsockets.h
    src/modules/avatar/api/avatarapi.h
    src/bridge/avatarpreviewscene.h
    src/bridge/avatarpreview.h
    src/viewport/boneoverlay.h
    src/export/gltfexporter.h
    src/export/exportservice.h
    src/export/previewlauncher.h
    src/export/embeddedpreview.h
    src/export/walkers/scenewalker.h
    src/export/walkers/meshbufferreader.h
    src/export/walkers/materialtexturereader.h
    src/export/exportmanifest.h
    src/export/exportcontentsource.h
    src/export/rawexporter.h
    src/modules/studiomodule.h
    src/scripting/modules/studiomodules.h
    src/scripting/mcp/mcpserver.h
    src/scripting/mcp/mcptools.h
    src/scripting/claude/claudecliprobe.h
    src/scripting/claude/claudelaunchconfig.h
    src/scripting/claude/claudestreamparser.h
    src/scripting/claude/claudechathost.h
    src/ui/windows/claudechatwindow.h
    src/ui/dialogs/preferences/mcpsettingswidget.h
    src/ui/dialogs/preferences/cachesettingswidget.h
    src/ui/dialogs/preferences/assetssettingswidget.h
    src/services/services.h
    src/services/assetimporter.h
    src/services/assetmetadata.h
    src/services/assetcas.h
    src/services/assetgc.h
    src/services/assetmigration.h
    src/services/assetstore.h
    src/services/assetstorepaths.h
    src/data/database/casschema.h
    src/services/audiopeaks.h
    src/services/videoutils.h
    src/services/projectservice.h
    src/services/imagematerial.h
    src/services/sceneeditservice.h
    src/services/thumbnailservice.h
    src/services/undoservice.h
    src/services/selectionservice.h
    src/services/shortcutregistry.h
    src/services/worldmodes.h
    src/services/scenefolders.h
    src/services/planarreflectors.h
    src/services/animationedits.h
    src/services/playbackservice.h
    src/ui/panels/presets/assetpanel.h 
    src/ui/panels/presets/assetmodelpanel.h 
    src/ui/panels/presets/assetmaterialpanel.h 
    src/ui/controls/accordionbladewidget.h 
    src/ui/panels/transformeditor.h 
    src/ui/controls/dragspinbox.h 
    src/ui/panels/scenehierarchywidget.h 
    src/viewport/orbitalcameracontroller.h 
    src/viewport/cameracontrollerbase.h 
    src/ui/panels/presets/skypresets.h 
    src/ui/panels/propertywidgets/transformpropertywidget.h 
    src/ui/panels/propertywidgets/lightpropertywidget.h 
    src/ui/panels/propertywidgets/decalpropertywidget.h 
    src/ui/panels/propertywidgets/materialpropertywidget.h 
    src/ui/panels/propertywidgets/worldskypropertywidget.h
    src/ui/panels/propertywidgets/worldgipropertywidget.h 
    src/ui/panels/propertywidgets/worldaapropertywidget.h
    src/ui/panels/propertywidgets/worldmodespropertywidget.h
    src/ui/panels/propertywidgets/worldshadowpropertywidget.h
    src/ui/panels/propertywidgets/skypropertywidget.h 
    src/ui/panels/scenenodepropertieswidget.h 
    src/ui/panels/propertywidgets/worldpropertywidget.h 
    src/ui/panels/propertywidgets/physicspropertywidget.h 
    src/ui/panels/propertywidgets/cubemapwidget.h
    src/io/scenewriter.h 
    src/io/scenereader.h 
    src/ui/panels/propertywidgets/scenepropertywidget.h 
    src/services/thumbnailmanager.h 
    src/ui/panels/propertywidgets/fogpropertywidget.h 
    src/io/assetiobase.h 
    src/data/materialpreset.h 
    src/io/materialpresetreader.h 
    src/ui/panels/timeline/keyframelabeltreewidget.h 
    src/io/materialpresetreader.h 
    src/viewport/gizmo.h 
    src/viewport/gizmomeshes.h 
    src/viewport/translationgizmo.h 
    src/viewport/rotationgizmo.h 
    src/viewport/scalegizmo.h 
    src/viewport/keyboardstate.h 
    src/viewport/editordata.h 
    src/ui/panels/propertywidgets/emitterpropertywidget.h 
    src/ui/panels/propertywidgets/nodepropertywidget.h
    src/ui/controls/labelwidget.h
    src/ui/controls/checkboxwidget.h 
    src/ui/controls/textinputwidget.h 
    src/ui/controls/comboboxwidget.h 
    src/ui/controls/hfloatsliderwidget.h 
    src/ui/controls/particlerampwidget.h
    src/ui/controls/texturepickerwidget.h 
    src/io/materialreader.h 
    src/ui/controls/filepickerwidget.h 
    src/ui/panels/propertywidgets/meshpropertywidget.h 
    src/ui/panels/propertywidgets/shaderpropertywidget.h 
    src/ui/panels/propertywidget.h 
    src/ui/controls/basewidget.h 
    src/ui/dialogs/newprojectdialog.h 
    src/ui/panels/assetwidget.h 
    src/io/assetmanager.h 
    src/ui/controls/assetpickerwidget.h 
    src/data/constants.h 
    src/ui/panels/timeline/keyframelabel.h 
    src/ui/panels/timeline/keyframecurvewidget.h 
    src/ui/panels/timeline/animationwidgetdata.h 
    src/ui/dialogs/getnamedialog.h 
    src/ui/panels/timeline/createanimationwidget.h 
    src/data/database/database.h 
    src/data/database/projectdatabase.h 
    src/data/guidmanager.h 
    src/commands/transformscenenodecommand.h 
    src/commands/changematerialpropertycommand.h
    src/commands/changematerialcommand.h
    src/commands/addscenenodecommand.h
    src/commands/deletescenenodecommand.h
    src/commands/reparentscenenodecommand.h
    src/commands/scenefoldercommand.h
    src/commands/setnodepropertycommand.h
    src/commands/nodeeditcommand.h
    src/ui/dialogs/progressdialog.h 
    src/ui/dialogs/screenshotwidget.h 
    src/ui/pages/projectmanager.h 
    src/ui/controls/dynamicgrid.h
    src/ui/controls/itemgridwidget.h
    src/ui/controls/sliderlayoutmodel.h
    src/ui/dialogs/renameprojectdialog.h 
    src/services/thumbnailgenerator.h 
    src/ui/dialogs/donatedialog.h
    src/ui/pages/assetview.h
    src/ui/pages/importviewertail.h
    src/ui/pages/previewrouter.h
    src/ui/pages/iassetviewer.h
    src/ui/pages/headlessassetviewer.h
    src/bridge/engineassetscene.h
    src/ui/pages/engineassetviewer.h
    src/bridge/enginematerialpreviewscene.h
    src/bridge/enginematerialpreview.h
    src/ui/controls/assetviewgrid.h
    src/ui/controls/assetgriditem.h
    src/ui/controls/drawertreewidget.h
    src/ui/controls/videopreviewwidget.h
    src/ui/controls/waveformwidget.h
    src/app/breakpad.h
    src/app/updatechecker.h
    src/ui/dialogs/softwareupdatedialog.h
    src/services/assethelper.h
    src/services/subscriber.h
    src/services/scenenodehelper.h
    src/app/upgrader.h
    thirdparty/qtawesome/QtAwesome.h
    thirdparty/qtawesome/QtAwesomeAnim.h 
    src/ui/style/stylesheet.h
    src/ui/style/thememanager.h
    src/ui/controls/colorview.h
    src/ui/dialogs/toast.h
    src/ui/controls/tooltip.h
    src/ui/dialogs/dialoghelper.h

    # player
    src/player/playerwidget.h
    src/player/engineplayerscene.h
    src/player/engineplayerview.h
    src/player/playermousecontroller.h
    src/player/playback.h
)

# Todo - trim these down to nothing eventually
set(WIDGETS
    src/shell/mainwindow.ui 
    src/ui/controls/colorvaluewidget.ui 
    src/ui/controls/colorpickerwidget.ui 
    src/ui/dialogs/preferencesdialog.ui 
    src/ui/dialogs/preferences/worldsettings.ui 
    src/ui/dialogs/aboutdialog.ui 
    src/ui/dialogs/infodialog.ui 
    src/ui/dialogs/loadmeshdialog.ui 
    src/ui/panels/timeline/animationwidget.ui 
    src/ui/controls/accordionbladewidget.ui 
    src/ui/panels/scenehierarchywidget.ui 
    src/ui/panels/presets/skypresets.ui 
    src/ui/panels/timeline/keyframelabeltreewidget.ui 
    src/ui/controls/labelwidget.ui 
    src/ui/controls/checkboxwidget.ui 
    src/ui/controls/textinputwidget.ui 
    src/ui/controls/comboboxwidget.ui 
    src/ui/controls/hfloatsliderwidget.ui 
    src/ui/controls/texturepickerwidget.ui 
    src/ui/controls/filepickerwidget.ui 
    src/ui/panels/propertywidget.ui 
    src/ui/dialogs/newprojectdialog.ui 
    src/ui/panels/assetwidget.ui 
    src/ui/controls/assetpickerwidget.ui 
    src/ui/panels/timeline/keyframelabel.ui 
    src/ui/panels/timeline/keyframecurvewidget.ui 
    src/ui/dialogs/getnamedialog.ui 
    src/ui/panels/timeline/createanimationwidget.ui 
    src/ui/dialogs/progressdialog.ui 
    src/ui/dialogs/screenshotwidget.ui 
    src/ui/pages/projectmanager.ui 
    src/ui/dialogs/renameprojectdialog.ui 
    src/ui/dialogs/donate.ui
    src/ui/dialogs/softwareupdatedialog.ui
)

# The engine abstraction lives in the IrisGL repo (owner decision 2026-08-30:
# Studio = the app; IrisGL = document + engine). It stays a separate library —
# the document never links it — and keeps build-linux/engine as its binary dir
# so test and tooling paths are unchanged.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/irisgl/engine ${CMAKE_CURRENT_BINARY_DIR}/engine)

# Headless engine tests (tests/). include(CTest) defines BUILD_TESTING and calls
# enable_testing() at top level, which add_test in a subdirectory requires.
# Which editor viewport starts by default. Overridable at runtime with
# --viewport=engine|legacy or env JAHSHAKA_VIEWPORT (see src/bridge/enginehost.h).
option(JAHSHAKA_ENGINE_VIEWPORT "Default to the engine viewport" OFF)

# libxcb (Linux only): EmbeddedWebPreview walks the X window tree on Qt's own
# xcb connection to adopt the Chrome preview window into the Publish page.
# Optional — without it the publish preview always uses the companion window
# (JAH_HAVE_XCB unset). Detected before tests/ so test targets see it too.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(JAH_XCB QUIET IMPORTED_TARGET xcb)
        if(JAH_XCB_FOUND)
            add_compile_definitions(JAH_HAVE_XCB)
        endif()
    endif()
endif()

option(JAHSHAKA_BUILD_TESTS "Build the headless engine test suite" ON)
if(JAHSHAKA_BUILD_TESTS)
    include(CTest)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

# Qlementine (thirdparty/qlementine submodule, MIT © Olivier Cléro, pinned v1.4.2):
# the app-wide QStyle that replaces the per-widget dark stylesheets. Library only —
# QLEMENTINE_SANDBOX/QLEMENTINE_SHOWCASE stay off.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/qlementine)

set(LIBS Qt6::Concurrent Qt6::Sql Qt6::Svg Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::Qml Qt6::HttpServer IrisGL JahshakaEngine qlementine ${OPENGL_LIBRARIES})
if(JAH_XCB_FOUND)
    list(APPEND LIBS PkgConfig::JAH_XCB)
endif()

include(IncludeBreakpad)
include(IncludeMaterialsModule)

set (QRCS
    ${CMAKE_SOURCE_DIR}/app/fonts.qrc
    ${CMAKE_SOURCE_DIR}/app/icons.qrc
    ${CMAKE_SOURCE_DIR}/app/images.qrc
    ${CMAKE_SOURCE_DIR}/app/models.qrc
    ${CMAKE_SOURCE_DIR}/app/shaders.qrc
    ${CMAKE_SOURCE_DIR}/app/themes.qrc
    ${CMAKE_SOURCE_DIR}/src/export/viewer/viewer.qrc
    ${CMAKE_SOURCE_DIR}/src/scripting/claude/claudeskills.qrc
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

add_executable(${CMAKE_PROJECT_NAME} MACOSX_BUNDLE src/app/main.cpp ${SRCS} ${HEADERS} ${QRCS} ${WIDGETS} app/winapp.rc ${BUNDLE_ICON}
    src/app/cli/clioptions.h
    src/app/cli/scriptrunner.h
    src/app/cli/selftestrunner.h
    src/app/versionsplashscreen.h src/app/versionsplashscreen.cpp
    src/app/shaderbuildgate.h src/app/shaderbuildgate.cpp)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${APP_OUTPUT_NAME})

if(APPLE)
    # The app is a bundle, so applicationDirPath() — where it extracts its app/
    # content tree on first run — is Contents/MacOS inside the bundle. The
    # standalone test binaries live in bin/ and resolve applicationDirPath()/app
    # there. Expose the bundle's tree to them via a symlink (gate-verified:
    # turns thumbnails/assets/materialpreview engine suites green on macOS).
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/app"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/app")
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${CMAKE_PROJECT_NAME})

# The executable finds its bundled shared libs beside itself when installed
# (the tests already set an RPATH; the app never did - audit §5.4.14).
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES INSTALL_RPATH "$ORIGIN")

# Minimal install story (audit §5.4.8): the binary and the runtime media tree
# (Hlms shader templates) it resolves relative to itself.
include(GNUInstallDirs)
install(TARGETS ${CMAKE_PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
install(DIRECTORY ${JAHSHAKA_MEDIA_DIR} DESTINATION ${CMAKE_INSTALL_BINDIR})
target_include_directories(${PROJECT_NAME} PUBLIC
                            src

                            irisgl
                            thirdparty/breakpad/breakpad/src
                            downloader)

# assimp is DECLARED, not inherited (ENGINEERING_DEBT_SPEC item 5, the 0.5 d
# slice). Studio picked assimp's headers and link flags up transitively through
# IrisGL's PUBLIC dependency; on macOS that is exactly where dylib install-name
# / rpath problems surface, and a transitive dependency is much harder to
# diagnose than a declared one.
#
# Exactly two Studio TUs need assimp directly, both parsing outside IrisGL:
#   src/services/assetmetadata.cpp          - its own Assimp::Importer for the
#                                             per-type model metadata block
#   src/services/import/assetimportservice.cpp - assimp/version.h for the
#                                             import determinism record
# (Studio also *names* aiScene/AssimpObject through IrisGL's own public
# headers - assethelper.cpp, assetview.cpp - which is IrisGL's dependency to
# declare, not ours. True PRIVATE-ization of IrisGL's link stays blocked on
# step 13; see the spec's item 5.)
set(JAH_ASSIMP_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include)
target_include_directories(${PROJECT_NAME} PRIVATE ${JAH_ASSIMP_INCLUDE_DIRS})

#force build downloader
#luckily, the jahshaka project has all the dependencies of the downloader project
#so we dont need to run windeployqt or macdeployqt on the downloader specifically
add_dependencies(${CMAKE_PROJECT_NAME} ${downloaderProject})

if(USE_BREAKPAD)
    set(LIBS ${LIBS} breakpad)
endif(USE_BREAKPAD)

# Keyword signature so `assimp` can be spelled PRIVATE beside the rest (see the
# assimp note above). CMake forbids mixing the plain and keyword forms on one
# target; the app is an executable, so PRIVATE is what the plain form already
# meant in practice.
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${LIBS} assimp)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE JAHSHAKA_VERSION="${JAHSHAKA_VERSION}")
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

# The Qt-Installer-Framework config has a WIN32 branch and a LINUX branch and
# nothing else: on APPLE it leaves PLATFORM_DATA_DIR undefined and then uses it
# in a copy_directory (deploy/installer/config.cmake:57-66), so a Release
# configure fails outright. macOS ships as a signed .app via
# scripts/make-macos-bundle.sh, not as an IFW installer — exclude it.
if((WIN32 OR (UNIX AND NOT APPLE)) AND CMAKE_BUILD_TYPE STREQUAL "Release")
    string(TIMESTAMP CURRENT_DATE "%Y-%m-%d")
    set(RELEASE_DATE "${CURRENT_DATE}")
    set(CMAKE_PROJECT_VERSION "0.9.0")
    include(${CMAKE_SOURCE_DIR}/deploy/installer/config.cmake)
endif()
