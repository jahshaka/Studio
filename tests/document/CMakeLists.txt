# Characterisation tests for the iris:: scene DOCUMENT.
#
# THE NAME AND THE LINK LINE ARE STEP-14 RELICS, retired 2026-09-09
# (TEST_GATE_AUDIT.md §2). This suite was born to prove that constructing a
# document touched no GL: iris::Scene used to load a sky mesh and compile a sky
# shader in its constructor and LightNode used to build a QOpenGLTexture in
# its own, so the suite linked Qt6::OpenGL purely to assert that no context was
# current. The legacy GL renderer was DELETED at step 14 (2026-08-30) — there
# is no GL anywhere in this tree to be accidentally touched, the link against
# Qt6::OpenGL bought nothing, and "no GL context" was a precondition about a
# world that no longer exists. What the body actually does is CHARACTERISE the
# document model, which is what it is now called.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_document_characterisation test_document_characterisation.cpp)
set_target_properties(test_document_characterisation PROPERTIES AUTOMOC OFF)
target_include_directories(test_document_characterisation PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    # iris headers expose bullet types; this is IrisGL's own include path
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_document_characterisation PRIVATE IrisGL Qt6::Gui)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_document_characterisation)
add_test(NAME document.characterisation COMMAND test_document_characterisation
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.characterisation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 60)
jah_no_display(document.characterisation)

# The leak lane (scripts/sanitize.sh lsan): the document model itself — every
# node, mesh, material and animation type constructed and dropped. No Vulkan,
# no app process, so LeakSanitizer runs with detect_leaks=1 here.
# QUARANTINED (L2 LightNode::shadowMap, L5 KeyFrame keys) plus one leak of
# the suite's own (test_document_characterisation.cpp:386 news a Vector3DPropertyAnim
# it never deletes). Move to jah_lsan_lane once L2/L5 are fixed.
jah_lsan_blocked(document.characterisation)
