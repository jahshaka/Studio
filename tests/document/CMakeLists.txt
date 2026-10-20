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
# A model read from a Qt RESOURCE (smoke L10 item 4): the ":/" read path hands
# assimp bytes from memory, the one path where its 200-byte format sniff decides.
qt_add_resources(test_document_characterisation "document_fixtures"
    PREFIX "/document-fixtures" BASE fixtures FILES fixtures/commented_header.obj)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_document_characterisation)
add_test(NAME document.characterisation COMMAND test_document_characterisation
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
jah_ship_row(document.characterisation)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(document.characterisation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 60)
jah_no_display(document.characterisation)

# physics.mt_determinism (archived: archive/tests/physics) / physics.step_cost (archived: archive/tests/physics) / physics.step_overlap (archived: archive/tests/physics) — THE PHYSICS STEP ON BULLET'S MULTI-THREADED WORLD
# (ENGINE-UPGRADE-U4 stage 6, decisions 20/21/23). A pile of boxes dropped in layers, stepped by the document at the
# fixed step, frames counted; Bullet's own scheduler bound on the test's thread (its game thread) at the box's share.
# The GUARD (500 bodies, document only): single- vs multi-threaded and run to run, BIT-EQUAL before the first contact
# and within the derived bound (the landing's sqrt(2gh) dt once, g dt^2 a step) through the settling (16 steps; the
# growth to 32 printed) — Bullet MT is not bit-equal by construction.
# The MEASURES (by hand): the step at 10,000 bodies, 1 thread against 7, in play with the mirror's extract (its share
# of a 60 Hz frame, the fast path serving every body), and the core budget's question — the step's overlap with a
# free-running engine's pool spans.
add_executable(test_physics_step_cost test_physics_step_cost.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_physics_step_cost PROPERTIES AUTOMOC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_physics_step_cost PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_physics_step_cost PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_physics_step_cost PRIVATE IrisGL JahshakaEngine Qt6::Gui)
# It inlines Bullet's header math: never contract, as Bullet and the physics TUs (irisgl/CMakeLists.txt, BULLET-BATCHING-1).
target_compile_options(test_physics_step_cost PRIVATE ${_jah_bake_fp})
add_dependencies(test_physics_step_cost JahshakaEngineMedia)
# physics.many_movers — THE OWNER'S REAL CASE, MANY MOVERS PER FRAME (THREADING-1 stage 2): 1,000 bodies in play,
# stepped on the threads the ONE core budget gives the step (--threads budget: the game thread + the foreground share,
# what the app binds) with the mirror syncing every moved body, the bar on the step + the sync per frame, median past
# a 20-frame warm-up, at most a 60 Hz frame (16.7 ms) in Debug. The arithmetic before the row (the preflight): ~3.9 ms
# step (3.9 us/body, spikes/bullet-batching-1/s3-arms.txt) + 1.0-1.7 ms sync = 5-6.5 ms. Red by construction when the
# budget's foreground share starves Bullet. The step and the sync are printed apart (PHYSICS_MANY_MOVERS).
# A GUARD, not a measure: the bar is a whole 60 Hz frame against ~5 ms, a margin no parallel row on the card's lock
# closes — so it runs at every merge it is reached by, not only at the ship.
jah_gpu_row(physics.many_movers SHIP CLASS engine COMMAND test_physics_step_cost --bodies 1000
         --frames 120 --warmup 20 --threads budget --mirror --mt-only --bar 16.7
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(physics.many_movers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document"
    TIMEOUT 60 JAHSHAKA_TEST_FRAMES 0)   # the mirror syncs; the engine renders nothing (physics.step_cost's metering)

# The leak lane (scripts/sanitize.sh lsan): the document model itself — every
# node, mesh, material and animation type constructed and dropped. No Vulkan,
# no app process, so LeakSanitizer runs with detect_leaks=1 here.
jah_lsan_lane(document.characterisation)

# document.no_silent_setters (archived: archive/tests/document) — the STATIC half of the dirty-set law
# (SPECS/DIRTY_SET_MIRROR_SPEC.md §3.8 item 3), shaped like theme.no_raw_sheets (archived: archive/tests/theme).
# Since the mirror only looks at a node the DOCUMENT says changed, a setter that
# writes a reflected field and reports nothing is a change that never reaches
# the screen — not a crash, not visible in any pixel test, and healed a second
# or two later by the background verifier, so the only symptom is lateness. This
# greps every `void set*()` under irisgl/document/{scenegraph,materials} for a
# body that assigns a member and never reaches notifyChanged / markedParams /
# touch. It caught thirteen emitter setters and the whole camera lens surface.
find_package(Python3 COMPONENTS Interpreter QUIET)

# document.reader_defaults (archived: archive/tests/document) — THE READER-DEFAULTS LAW, as a command (render audit
# I-5, lane READER-DEFAULTS-1), shaped like document.no_silent_setters (archived: archive/tests/document) and
# theme.no_raw_sheets (archived: archive/tests/theme): a lint over the source, because the class it guards
# against is a LITERAL somebody types, not a value somebody can assert.
#
# `src/io/scenereader.cpp` default-constructs the document object and overwrites
# only the keys the file carries, so every stated fallback must be the object's
# own value — `node->x`, never `1.0`. Two definitions of one default drift, and
# when they drift "never saved" and "saved without the key" become two different
# scenes: the shadow type (SUN1), `exposure` (which shipped five samples at half
# brightness), a light's radius, a light's shadow resolution, the explorer
# camera's far clip and six particle fields have all been that bug. The script
# carries the documented exceptions — a quaternion identity, a retired key on its
# way to the bin, a node type, a sentinel — each with its reason, and FAILS both
# on a new literal and on an exception that no longer matches anything.
#
# (Why not a write/strip/read round trip: nothing in this tree links SceneReader
# — it needs the library database, the asset store, the material helper, the bake
# store and the load timeline. tests/mirror and tests/defaults record the same
# constraint. The runtime half is scene.reopen_fidelity, which opens real archives
# with keys missing.)

# (document.mesh_cache is DELETED — ATOM P2, 2026-09-22. It tested the parse
# cache behind `Mesh::loadMesh`, and both are gone: nothing outside an import
# parses a model, the shipped meshes are baked library assets, and what used to
# be "one file is parsed once" is now "one asset is baked once"
# (meshbake.primitives_baked (archived: archive/tests/meshbake)).)

