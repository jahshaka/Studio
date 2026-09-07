# SPIKES — experiments, NOT gates.
#
# Everything here builds a runnable binary and NOTHING here is registered with
# add_test: a spike answers a question once, for a decision, and a suite that
# runs on every gate is a different thing with different obligations (it must
# never flake, it must be cheap, and it must keep passing). Run them by hand:
#
#   DISPLAY=:NN ./build-linux/tests/spikes/spike_perview_exposure
#
# spike_perview_exposure — CAMERA_LENS_SPEC §7 R1: does a setNamedConstant
# written from a per-View CompositorWorkspaceListener's workspacePreUpdate take
# effect for THAT workspace's passes in the same frame? P3/P4/P5's per-camera
# tuning all ride the answer. The engine-side listener it drives
# (chain::GlobalsListener + View::setPerViewGlobalsSpike) is SPIKE code too.
add_executable(spike_perview_exposure EXCLUDE_FROM_ALL spike_perview_exposure.cpp)
set_target_properties(spike_perview_exposure PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(spike_perview_exposure PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl)
target_compile_definitions(spike_perview_exposure PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(spike_perview_exposure PRIVATE JahshakaEngine X11)
add_dependencies(spike_perview_exposure JahshakaEngineMedia)
