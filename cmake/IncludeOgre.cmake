# Ogre-Next 4.0 — Jahshaka's engine backend.
#
# Consumed as an ORDINARY LIBRARY: no add_subdirectory of the engine, no generated
# registries. This is the property that made Ogre-Next viable where O3DE was not.
#
# Vulkan is the shipping backend. GL3Plus cannot do multiple on-screen windows
# (single mGlobalVao, OgreGL3PlusRenderSystem.cpp:840) — see OGRE_MIGRATION_SPEC.md §11.

set(OGRE_NEXT_PREFIX "$ENV{HOME}/Developer/engines/ogre-next-install"
    CACHE PATH "Ogre-Next install prefix (written by irisgl/scripts/build-ogre.sh)")
# The source tree ships as an irisgl submodule (pinned upstream + our patches,
# applied by the build script). The old external-checkout path is the fallback.
if(EXISTS "${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next/CMakeLists.txt")
    set(_ogre_src_default "${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next")
else()
    set(_ogre_src_default "$ENV{HOME}/Developer/engines/ogre-next")
endif()
set(OGRE_NEXT_SOURCE "${_ogre_src_default}"
    CACHE PATH "Ogre-Next source tree (for the Hlms shader templates under Samples/Media)")

if(NOT EXISTS "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Ogre.h")
    message(FATAL_ERROR
        "Ogre-Next not found at ${OGRE_NEXT_PREFIX}.\n"
        "One-time setup: git submodule update --init irisgl/thirdparty/ogre-next && "
        "irisgl/scripts/build-ogre.sh   (details: irisgl/docs/OGRE_BUILD.md)\n"
        "Or point -DOGRE_NEXT_PREFIX=<prefix> at an existing install.")
endif()
if(NOT EXISTS "${OGRE_NEXT_SOURCE}/Samples/Media/Hlms/Pbs")
    message(FATAL_ERROR
        "Ogre-Next Hlms templates not found at ${OGRE_NEXT_SOURCE}/Samples/Media/Hlms.\n"
        "Run: git submodule update --init irisgl/thirdparty/ogre-next, or set -DOGRE_NEXT_SOURCE=<source tree>.")
endif()

add_library(OgreNext INTERFACE)
# The install ships OgreBuildSettings.h (identical to the build tree's copy), so
# the source tree is NOT on the include path.
target_include_directories(OgreNext INTERFACE
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Common
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Pbs
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Unlit)
# Absolute paths, not -l names: link directories do not propagate through a
# static library's PRIVATE link, so -lOgreNextMain would not resolve in Jahshaka.
foreach(_lib OgreNextMain OgreNextHlmsPbs OgreNextHlmsUnlit)
    # Never trust a cached hit: a changed OGRE_NEXT_PREFIX must re-resolve.
    unset(OGRE_NEXT_${_lib}_LIB CACHE)
    find_library(OGRE_NEXT_${_lib}_LIB NAMES ${_lib}
                 PATHS ${OGRE_NEXT_PREFIX}/lib NO_DEFAULT_PATH)
    if(NOT OGRE_NEXT_${_lib}_LIB)
        message(FATAL_ERROR "Ogre-Next library ${_lib} not found in ${OGRE_NEXT_PREFIX}/lib")
    endif()
    target_link_libraries(OgreNext INTERFACE ${OGRE_NEXT_${_lib}_LIB})
endforeach()

# Runtime locations. These are DEFAULTS the host may fall back to; the engine
# itself takes them at runtime through EngineConfig (see engine/CMakeLists.txt,
# which exposes them to the host and stages the Hlms templates next to the exe).
set(OGRE_NEXT_PLUGIN_DIR "${OGRE_NEXT_PREFIX}/lib/OGRE-Next")
set(OGRE_NEXT_HLMS_SOURCE_DIR "${OGRE_NEXT_SOURCE}/Samples/Media/Hlms")
# Where the build stages runtime media next to the executable (engine/ fills it).
# Set here, at root scope, so engine/ and tests/ both see it.
set(JAHSHAKA_MEDIA_DIR "${CMAKE_BINARY_DIR}/bin/media")

message(STATUS "Ogre-Next: ${OGRE_NEXT_PREFIX}")
