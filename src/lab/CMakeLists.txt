# jahlab — THE ENGINE-ONLY HARNESS (LAB-1; SPECS/LAB_1_SPEC.md, DOCS/LAB.md). A second binary beside Jahshaka in
# build-linux/bin (media resolves from the binary's directory, the app's rule), linking the engine, irisgl's document /
# import / mirror (the mirror is compiled in, as every host of it does) and Qt Core/Gui — nothing of Studio's src/.
if(NOT UNIX OR APPLE)
    return()   # the window is Xlib's (labwindow.cpp); the Mac's lab is a later port
endif()
find_package(X11 REQUIRED)
add_executable(jahlab
    main.cpp
    labcamera.cpp
    labclasses.cpp
    labcommands.cpp
    laboverlay.cpp
    labscene.cpp
    labscripts.cpp
    labsession.cpp
    labwindow.cpp
    envs/envbuild.cpp
    envs/catalogue.cpp
    envs/atom_street.cpp
    envs/atom_arcade.cpp
    envs/atom_forest.cpp
    envs/atom_occlusion.cpp
    envs/atom_small.cpp
    envs/atom_flat.cpp
    envs/photon_room.cpp
    envs/photon_rough.cpp
    envs/photon_area.cpp
    envs/photon_court.cpp
    envs/photon_seam.cpp
    envs/photon_shells.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(jahlab PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "$ORIGIN")
target_include_directories(jahlab PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
# THE TREE's PATHS ON THE ONE TU THAT READS THEM (the build-speed audit D1: target-wide, they put this tree's path on all 24
# of jahlab's compile lines - every one a ccache miss in every other tree; enginehost.cpp's precedent, CMakeLists.txt)
set_property(SOURCE labsession.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    JAHLAB_OGRE_PLUGIN_DIR_DEFAULT="${OGRE_NEXT_PLUGIN_DIR}"
    JAHLAB_OGRE_MEDIA_DIR_DEFAULT="${JAHSHAKA_MEDIA_DIR}/")
# the engine's script host is an archive of its own (SCRIPT-IN-ENGINE-1 2b): jahlab is a client of it, as jahengine is
target_link_libraries(jahlab PRIVATE IrisGL JahshakaEngine JahshakaScript Qt6::Gui ${X11_LIBRARIES})
# THE STILLNESS DOOR'S CLASS IMAGE AT -O2 IN DEBUG (PHOTON-2 stage 0; labclasses.h says why).
set_source_files_properties(labclasses.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
add_dependencies(jahlab JahshakaEngineMedia)
