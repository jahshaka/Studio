# Ogre-Next 4.0 — Jahshaka's engine backend.
#
# Consumed as an ORDINARY LIBRARY: no add_subdirectory of the engine, no generated
# registries. This is the property that made Ogre-Next viable where O3DE was not.
#
# Vulkan is the shipping backend. GL3Plus cannot do multiple on-screen windows
# (single mGlobalVao, OgreGL3PlusRenderSystem.cpp:840) — see OGRE_MIGRATION_SPEC.md §11.

set(OGRE_NEXT_PREFIX "$ENV{HOME}/Developer/engines/ogre-next-install"
    CACHE PATH "Ogre-Next install prefix")
set(OGRE_NEXT_SOURCE "$ENV{HOME}/Developer/engines/ogre-next"
    CACHE PATH "Ogre-Next source tree (for OgreBuildSettings.h and Hlms media)")

if(NOT EXISTS "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Ogre.h")
    message(FATAL_ERROR
        "Ogre-Next not found at ${OGRE_NEXT_PREFIX}.\n"
        "Build it per OGRE_PLATFORM_DEPS.md, or set -DOGRE_NEXT_PREFIX=<prefix>.")
endif()

add_library(OgreNext INTERFACE)
target_include_directories(OgreNext INTERFACE
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Common
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Pbs
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Unlit
    ${OGRE_NEXT_SOURCE}/build/include)          # OgreBuildSettings.h
# Absolute paths, not -l names: link directories do not propagate through a
# static library's PRIVATE link, so -lOgreNextMain would not resolve in Jahshaka.
foreach(_lib OgreNextMain OgreNextHlmsPbs OgreNextHlmsUnlit)
    find_library(OGRE_NEXT_${_lib}_LIB NAMES ${_lib}
                 PATHS ${OGRE_NEXT_PREFIX}/lib NO_DEFAULT_PATH)
    if(NOT OGRE_NEXT_${_lib}_LIB)
        message(FATAL_ERROR "Ogre-Next library ${_lib} not found in ${OGRE_NEXT_PREFIX}/lib")
    endif()
    target_link_libraries(OgreNext INTERFACE ${OGRE_NEXT_${_lib}_LIB})
endforeach()

# Where the RenderSystem plugins and Hlms shader templates live at runtime.
# The Hlms data folders are REQUIRED, not optional samples.
target_compile_definitions(OgreNext INTERFACE
    JAHSHAKA_OGRE_PLUGIN_DIR="${OGRE_NEXT_PREFIX}/lib/OGRE-Next"
    JAHSHAKA_OGRE_MEDIA_DIR="${OGRE_NEXT_SOURCE}/Samples/Media/")

message(STATUS "Ogre-Next: ${OGRE_NEXT_PREFIX}")
