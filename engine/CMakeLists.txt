# The engine abstraction and its Ogre-Next backend.
# Studio links this; Studio never links Ogre directly.
add_library(JahshakaEngine STATIC
    src/OgreEngine.cpp
)
target_include_directories(JahshakaEngine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# OgreNext is PRIVATE on purpose: Ogre must not leak into Studio's include path.
target_link_libraries(JahshakaEngine PRIVATE OgreNext)
if(UNIX AND NOT APPLE)
    # Vulkan's XCB windowing path needs a Display* (SDL2x11 param) — backend detail.
    find_package(X11 REQUIRED)
    target_link_libraries(JahshakaEngine PRIVATE ${X11_LIBRARIES})
endif()
set_target_properties(JahshakaEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)
# No Qt in here, ever; the root enables AUTOMOC globally.
set_target_properties(JahshakaEngine PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# --- Runtime data: stage the Hlms shader templates next to the executable ----
# The engine resolves `<exe dir>/media/Hlms/{Common,Pbs,Unlit}` at runtime; the
# build-machine path is only a fallback default. ~600K, copied when stale.
set(_hlms_stamp "${CMAKE_CURRENT_BINARY_DIR}/hlms-media.stamp")
# Also Ogre's common low-level material scripts (sky, DPSM point-light shadow maps,
# depth utilities): Samples/Media/2.0/scripts/materials/Common, ~540K.
get_filename_component(_ogre_media_root "${OGRE_NEXT_HLMS_SOURCE_DIR}" DIRECTORY)
set(OGRE_NEXT_COMMON_MATERIALS_DIR "${_ogre_media_root}/2.0/scripts/materials/Common")
file(GLOB_RECURSE _hlms_inputs CONFIGURE_DEPENDS
     "${OGRE_NEXT_HLMS_SOURCE_DIR}/Common/*" "${OGRE_NEXT_HLMS_SOURCE_DIR}/Pbs/*"
     "${OGRE_NEXT_HLMS_SOURCE_DIR}/Unlit/*" "${OGRE_NEXT_COMMON_MATERIALS_DIR}/*"
     "${CMAKE_CURRENT_SOURCE_DIR}/media/Hlms/Jahshaka/*")
add_custom_command(OUTPUT "${_hlms_stamp}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${JAHSHAKA_MEDIA_DIR}/Hlms"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${OGRE_NEXT_HLMS_SOURCE_DIR}/Common" "${JAHSHAKA_MEDIA_DIR}/Hlms/Common"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${OGRE_NEXT_HLMS_SOURCE_DIR}/Pbs"    "${JAHSHAKA_MEDIA_DIR}/Hlms/Pbs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${OGRE_NEXT_HLMS_SOURCE_DIR}/Unlit"  "${JAHSHAKA_MEDIA_DIR}/Hlms/Unlit"
    # Jahshaka's own Hlms pieces (linear fog); registered by registerCommonMaterials().
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/media/Hlms/Jahshaka" "${JAHSHAKA_MEDIA_DIR}/Hlms/Jahshaka"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${OGRE_NEXT_COMMON_MATERIALS_DIR}" "${JAHSHAKA_MEDIA_DIR}/2.0/scripts/materials/Common"
    COMMAND ${CMAKE_COMMAND} -E touch "${_hlms_stamp}"
    DEPENDS ${_hlms_inputs}
    COMMENT "Staging Ogre-Next Hlms templates into bin/media/Hlms")
add_custom_target(JahshakaEngineMedia DEPENDS "${_hlms_stamp}")
add_dependencies(JahshakaEngine JahshakaEngineMedia)

# Compile-time DEFAULTS for the host. The engine never reads these; the host puts
# them into EngineConfig after trying <exe dir>/media and the env overrides.
target_compile_definitions(JahshakaEngine PUBLIC
    JAHSHAKA_OGRE_PLUGIN_DIR_DEFAULT="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_OGRE_MEDIA_DIR_DEFAULT="${JAHSHAKA_MEDIA_DIR}/")
