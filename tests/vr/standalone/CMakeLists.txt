# VR phase 1a — the OpenXR init-order spike (SPECS/VR_SPEC.md §5).
#
# A STANDALONE binary, deliberately: it drives Ogre-Next directly rather than
# through jahshaka::engine, because the question it exists to answer is the ORDER
# in which Root, the Vulkan plugin, the external instance and the external device
# must be created — and `Engine::create` has that order baked in with no
# external-instance seam (adding one is phase 2's work, VR_SPEC §4.1). Driving
# Ogre directly also keeps the lane's blast radius at zero tree files.
#
# Configure it against the tree's OWN Ogre install (never the legacy shared one):
#   cmake -S tests/vr/standalone -B <build> -G Ninja \
#         -DOGRE_NEXT_PREFIX=<tree>/irisgl/thirdparty/ogre-next-install \
#         -DOGRE_MEDIA_DIR=<tree>/irisgl/thirdparty/ogre-next/Samples/Media
cmake_minimum_required(VERSION 3.16)
project(jah_openxr_vulkan_spike CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT OGRE_NEXT_PREFIX)
    get_filename_component(_def "${CMAKE_CURRENT_SOURCE_DIR}/../../../irisgl/thirdparty/ogre-next-install" ABSOLUTE)
    set(OGRE_NEXT_PREFIX "${_def}" CACHE PATH "Ogre-Next install prefix")
endif()
if(NOT OGRE_MEDIA_DIR)
    get_filename_component(_defm "${CMAKE_CURRENT_SOURCE_DIR}/../../../irisgl/thirdparty/ogre-next/Samples/Media" ABSOLUTE)
    set(OGRE_MEDIA_DIR "${_defm}" CACHE PATH "Ogre-Next Samples/Media (the PATCHED source tree)")
endif()

if(NOT EXISTS "${OGRE_NEXT_PREFIX}/include/OGRE-Next/OgreRoot.h")
    message(FATAL_ERROR "No Ogre-Next install at ${OGRE_NEXT_PREFIX} — run irisgl/scripts/build-ogre.sh")
endif()

find_package(Vulkan REQUIRED)
find_package(OpenXR REQUIRED)

# The render system is installed UNPREFIXED (`RenderSystem_Vulkan.so`) — name the
# file with its suffix or find_library silently finds nothing (the trap recorded
# in irisgl/engine/CMakeLists.txt).
find_library(OGRE_NEXT_VULKAN_RS_LIB
             NAMES RenderSystem_Vulkan.so RenderSystem_Vulkan.dylib RenderSystem_Vulkan
             PATHS "${OGRE_NEXT_PREFIX}/lib/OGRE-Next" "${OGRE_NEXT_PREFIX}/lib"
             NO_DEFAULT_PATH REQUIRED)

add_executable(jah_xr_spike "${CMAKE_CURRENT_SOURCE_DIR}/../xr_spike.cpp")

target_include_directories(jah_xr_spike PRIVATE
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next"
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Common"
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Pbs"
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Unlit"
    # BOTH Vulkan header roots: the install flattens the render system's headers
    # into RenderSystems/Vulkan, but they include their siblings as
    # `Vao/OgreVulkanBufferInterface.h`, which only resolves under .../include.
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan"
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan/include"
    ${Vulkan_INCLUDE_DIRS})

target_link_directories(jah_xr_spike PRIVATE "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(jah_xr_spike PRIVATE
    OgreNextMain OgreNextHlmsPbs OgreNextHlmsUnlit
    "${OGRE_NEXT_VULKAN_RS_LIB}"
    ${Vulkan_LIBRARIES}
    OpenXR::openxr_loader)

target_compile_definitions(jah_xr_spike PRIVATE
    JAH_OGRE_PLUGIN_DIR="${OGRE_NEXT_PREFIX}/lib/OGRE-Next"
    JAH_OGRE_MEDIA_DIR="${OGRE_MEDIA_DIR}")

set_target_properties(jah_xr_spike PROPERTIES
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
