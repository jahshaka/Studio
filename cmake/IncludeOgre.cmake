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
# A stale cache from a pre-submodule configure staged UNPATCHED media silently
# (found 2026-09-03: patch 0009 missing from bin/media while the build succeeded).
# When the submodule exists, any cached value pointing elsewhere is force-corrected.
if(EXISTS "${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next/CMakeLists.txt"
   AND NOT OGRE_NEXT_SOURCE STREQUAL "${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next")
    message(WARNING "OGRE_NEXT_SOURCE pointed at '${OGRE_NEXT_SOURCE}' but the ogre-next "
                    "submodule exists — forcing it to the submodule so patched media stages.")
    set(OGRE_NEXT_SOURCE "${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next"
        CACHE PATH "Ogre-Next source tree (for the Hlms shader templates under Samples/Media)" FORCE)
endif()

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
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Unlit
    # Atmosphere component: its headers include each other by bare name.
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Atmosphere
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/PlanarReflections
    # Overlay component (STATS_OVERLAY_SPEC D1): the engine-drawn stats readout
    # and loading cover. Its headers include each other by bare name too.
    ${OGRE_NEXT_PREFIX}/include/OGRE-Next/Overlay)
# Plain -I, deliberately NOT SYSTEM: OGRE_BUILD_COMPONENT_PLANAR_REFLECTIONS is
# #ifdef-ed inside OgreHlmsPbs.h, so the installed OgreBuildSettings.h changes
# HlmsPbs's member LAYOUT. generateAbiCookie() does not hash component defines,
# so the only thing that catches a stale consumer is ninja's -MD dependency on
# that installed header. SYSTEM includes can switch a toolchain to -MMD (system
# headers dropped from depfiles) and would reintroduce a silent ABI mismatch.
#
# Absolute paths, not -l names: link directories do not propagate through a
# static library's PRIVATE link, so -lOgreNextMain would not resolve in Jahshaka.
# OgreNextOverlay is in this list on purpose: it is a `cmake_dependent_option`
# on FREETYPE_FOUND upstream, so `-DOGRE_BUILD_COMPONENT_OVERLAY=ON` on a box
# without freetype dev files SILENTLY yields OFF and produces a different
# install. build-ogre.sh greps its configure for the target for the same reason.
# Failing loudly here is the second half of that guard.
foreach(_lib OgreNextMain OgreNextHlmsPbs OgreNextHlmsUnlit OgreNextAtmosphere
             OgreNextPlanarReflections OgreNextOverlay)
    # Never trust a cached hit: a changed OGRE_NEXT_PREFIX must re-resolve.
    unset(OGRE_NEXT_${_lib}_LIB CACHE)
    find_library(OGRE_NEXT_${_lib}_LIB NAMES ${_lib}
                 PATHS ${OGRE_NEXT_PREFIX}/lib NO_DEFAULT_PATH)
    if(NOT OGRE_NEXT_${_lib}_LIB)
        message(FATAL_ERROR
            "Ogre-Next library ${_lib} not found in ${OGRE_NEXT_PREFIX}/lib.\n"
            "If this is OgreNextPlanarReflections (or any component that used to build), "
            "your engine install predates the current component pin: RE-RUN "
            "irisgl/scripts/build-ogre.sh. That script is the single source of truth for "
            "which Ogre components exist; the install is not reproducible without it.")
    endif()
    target_link_libraries(OgreNext INTERFACE ${OGRE_NEXT_${_lib}_LIB})
endforeach()

# Runtime locations. These are DEFAULTS the host may fall back to; the engine
# itself takes them at runtime through EngineConfig (see engine/CMakeLists.txt,
# which exposes them to the host and stages the Hlms templates next to the exe).
if(EXISTS "${OGRE_NEXT_PREFIX}/lib/OGRE-Next")
    set(OGRE_NEXT_PLUGIN_DIR "${OGRE_NEXT_PREFIX}/lib/OGRE-Next")
else()
    # macOS: the install puts RenderSystem_*.dylib directly in lib/ (no subdir).
    set(OGRE_NEXT_PLUGIN_DIR "${OGRE_NEXT_PREFIX}/lib")
endif()
set(OGRE_NEXT_HLMS_SOURCE_DIR "${OGRE_NEXT_SOURCE}/Samples/Media/Hlms")
# Where the build stages runtime media next to the executable (engine/ fills it).
# Set here, at root scope, so engine/ and tests/ both see it.
set(JAHSHAKA_MEDIA_DIR "${CMAKE_BINARY_DIR}/bin/media")

message(STATUS "Ogre-Next: ${OGRE_NEXT_PREFIX}")
