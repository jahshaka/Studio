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
    labcommands.cpp
    laboverlay.cpp
    labscene.cpp
    labsession.cpp
    labwindow.cpp
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
target_compile_definitions(jahlab PRIVATE
    JAHLAB_OGRE_PLUGIN_DIR_DEFAULT="${OGRE_NEXT_PLUGIN_DIR}"
    JAHLAB_OGRE_MEDIA_DIR_DEFAULT="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(jahlab PRIVATE IrisGL JahshakaEngine Qt6::Gui ${X11_LIBRARIES})
add_dependencies(jahlab JahshakaEngineMedia)
