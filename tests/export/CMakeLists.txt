# Web export tests (WEB_EXPORT_AUDIT phases 0-1): document -> GLB writer +
# export-folder assembly, validated structurally. Document-only (no GL, no
# engine) — runs offscreen. The viewer resources come from viewer.qrc, compiled
# into this test exactly as into the app.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_web_export test_web_export.cpp
    ${CMAKE_SOURCE_DIR}/src/export/gltfexporter.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportservice.cpp
    ${CMAKE_SOURCE_DIR}/src/export/previewlauncher.cpp
    ${CMAKE_SOURCE_DIR}/src/export/walkers/scenewalker.cpp
    ${CMAKE_SOURCE_DIR}/src/export/walkers/meshbufferreader.cpp
    ${CMAKE_SOURCE_DIR}/src/export/walkers/materialtexturereader.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/export/viewer/viewer.qrc)
set_target_properties(test_web_export PROPERTIES AUTOMOC OFF AUTORCC ON)
target_include_directories(test_web_export PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_web_export PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_web_export PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_web_export)
add_test(NAME export.web COMMAND test_web_export
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
jah_ship_row(export.web)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(export.web PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "export")
jah_no_display(export.web)

# Embedded preview (Linux/X11): failure paths, the RE-ENTRY state machine
# (stop -> fresh start, unique per-run profiles, stale-profile purge), the
# per-project PublishRecord (present/missing/none states), and the viewer
# readiness-marker contract. Runs on xcb (the feature's actual platform) — the
# suite already requires a reachable DISPLAY for the engine tests. Never
# spawns a real browser; successful adoption is rig-verified (needs Chrome +
# GPU).
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Shared export walkers (ASSET_PIPELINE_SPEC §3.3 phase-5 front half):
# traversal/skip/classification, scene inventory, mesh buffer readers.
# Document-only — runs offscreen like export.web.
# gltfexporter.cpp joins the sources for ONE reason (PUBLISH_AUDIT #5): the
# suite that owns the walkers is where the exported TRS round trip belongs —
# what the walkers hand out has to survive into the file. iesprofile.cpp is the
# exporter's only extra link dependency.
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.

# The leak lane (scripts/sanitize.sh lsan): the walkers and writers build whole
# document trees in memory. export.embed spawns a browser, so it stays out.
jah_lsan_lane( export.web )

