# assets.tray (archived: archive/tests/assets) — the EDITOR TRAY's listing and the Assets page's LIBRARY GRID,
# plus (DRAWERS-1) the PROJECT'S FOLDER MODEL behind assets.folders and its
# four mutating verbs: the rules, the refusals, where a filing is recorded for
# a pinned library row against a row the project owns, and the undo of a
# multi-row move as ONE step.
# as rules AND as queries (small-items round B). Builds the REAL Database,
# services/assettray.cpp and services/imagematerial.cpp against a throwaway
# SQLite file with a realistic library in it: the five collapse rules, the
# grid's membership rule, the project import's parent remap, and a COUNT of
# the SQL statements one listing (and one search keystroke) executes — the
# tray runs on every edge write and every keypress, so its cost is per-key.
# Headless (offscreen platform); never touches the user's live JahLibrary.db.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)

# THE STATEMENT COUNT comes from sqlite3_trace_v2 on the handle Qt's own
# QSQLITE driver opened (QSqlDriver::handle) — no instrumentation in the
# production path. Where the header/library is absent the suite still runs and
# still checks every RULE; it just does not assert the counts.
find_path(JAH_SQLITE3_INCLUDE sqlite3.h)
find_library(JAH_SQLITE3_LIB sqlite3)

# ---------------------------------------------------------------------------
# D11-LIBRARY-SCALE's two suites, on the same REAL Database link closure.
#
# library.model — the Library as a model over the tile cache: the item-model
# contract under QAbstractItemModelTester (Fatal), the O(1) guid lookup, NO
# LISTING SELECTS A THUMBNAIL (the query log: every thumbnail select keyed by
# guid), the tiles read by guid in batches and decoded off the UI thread, the
# predicates' indexes present and used by the planner.
# archive.closure (archived: archive/tests/archive) — the closure walk at one statement per BFS level and the
# payload at one header + one edge statement for the set, one files statement
# and one BLOB read per asset, answering exactly what the per-node walk did.
set(_d11_db_closure
    ${CMAKE_SOURCE_DIR}/src/services/assettray.cpp
    ${CMAKE_SOURCE_DIR}/src/services/imagematerial.cpp
    ${CMAKE_SOURCE_DIR}/src/services/materialmembers.cpp
    ${CMAKE_SOURCE_DIR}/src/services/memberstamp.cpp
    ${CMAKE_SOURCE_DIR}/src/services/materialbundle.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetdelete.cpp
    ${CMAKE_SOURCE_DIR}/src/services/projectmembership.cpp
    ${CMAKE_SOURCE_DIR}/src/services/projectassets.cpp
    ${CMAKE_SOURCE_DIR}/src/services/meshbakestore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/primitiveassets.cpp
    ${CMAKE_SOURCE_DIR}/src/services/loadtimeline.cpp
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/databasereads.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationfile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rigsignature.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp)
find_package(Qt6 REQUIRED COMPONENTS Concurrent Test Qml)
function(jah_d11_suite _target _test _source)
    add_executable(${_target} ${_source} ${_d11_db_closure} ${ARGN})
    set_target_properties(${_target} PROPERTIES AUTOMOC ON)
    target_include_directories(${_target} PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
    target_link_libraries(${_target} PRIVATE jahshaka_db IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::Concurrent Qt6::Test Qt6::Qml)
    add_test(NAME ${_test} COMMAND ${_target} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(${_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets" TIMEOUT 60)
    jah_no_display(${_test})
endfunction()
jah_d11_suite(test_library_model library.model test_library_model.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/librarymodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/tilecache.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/assetdrag.cpp
    ${CMAKE_SOURCE_DIR}/src/services/previewdemand.cpp)
jah_ship_row(library.model)   # THE SHIP SET (TEST-CUT-1)

# library.statements — LARGE-LIBRARY-1 stage 1: the library's statements, counted by SQLite's own
# statement trace on the default connection (data/database/databasereads.h), do not grow with the
# library; the trace's negative controls. THE STATED LIMIT (ruled call 3): only the default connection
# is traced — a per-asset query moved onto a worker connection would not be seen. The trace needs the
# SYSTEM SQLite the Qt driver uses, linked as the app links it (the Studio CMakeLists.txt's SQLite3 block).
jah_d11_suite(test_library_statements library.statements test_library_statements.cpp
    ${CMAKE_SOURCE_DIR}/tests/support/libraryfixture.cpp)
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    target_link_libraries(test_library_statements PRIVATE SQLite::SQLite3)
    target_compile_definitions(test_library_statements PRIVATE JAH_HAVE_SQLITE3=1)
endif()
target_compile_definitions(test_library_statements PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_tests_properties(library.statements PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;JAH_DB_STATEMENT_TRACE=1")

# library.scale — LARGE-LIBRARY-1 stage 2: 10,000 assets / 500 projects run as many statements as 1,000 / 50 on every
# step a returning user takes (the boot, open, the Assets page, Materials, the Desktop, create), with the boot's one-time
# index gate and the generator's real bakes in its first session per library (library_scale.sh says the laws). Two
# --headless sessions per generated root; the trace counts the DEFAULT connection only (ruled call 3: the workers'
# named reads are printed). `--headless` stands on the command line (gate-scope reads a headless script row from it).
set(_lscale_home ${CMAKE_CURRENT_BINARY_DIR}/home-library_scale)
file(MAKE_DIRECTORY ${_lscale_home}/run)
add_test(NAME library.scale
         COMMAND sh "${JAH_FRESH_HOME}" "${_lscale_home}" --
                 bash ${CMAKE_CURRENT_SOURCE_DIR}/library_scale.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --headless
                 $<TARGET_FILE:test_library_statements> ${CMAKE_CURRENT_SOURCE_DIR}/scripts/library_scale.js
                 ${CMAKE_CURRENT_SOURCE_DIR}/library_columns.py ${CMAKE_SOURCE_DIR}/tests/scripting/fixtures/implane_wide.png
         WORKING_DIRECTORY ${_lscale_home}/run)
set_tests_properties(library.scale PROPERTIES ENVIRONMENT "HOME=${_lscale_home}" LABELS "assets" TIMEOUT 60
                     SKIP_RETURN_CODE 77)
set_property(TEST library.scale APPEND PROPERTY ENVIRONMENT_MODIFICATION "JAHSHAKA_DATA_ROOT=unset:")
jah_no_display(library.scale)

# bundle.writer — EXPORT-THREAD-1: the share file's writer (services/bundlewriter.*) on a worker
# writes the same manifest + payload bytes as on the calling thread, for a 200-entry pack; the
# calling thread's event loop turns while it writes (a 1 ms timer ticks — a count); a cancelled
# write leaves the destination as it was; the READER's bound (unpackBundle) lets the 200-entry
# pack through and refuses a payload larger than its manifest accounts for. No database, no engine.
add_executable(test_bundle_writer test_bundle_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/services/bundlewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportmanifest.cpp
    ${CMAKE_SOURCE_DIR}/src/io/clipboardformat.cpp
    ${CMAKE_SOURCE_DIR}/src/io/ziphelper.cpp)
set_target_properties(test_bundle_writer PROPERTIES CXX_STANDARD 17)
target_include_directories(test_bundle_writer PRIVATE ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src)
target_link_libraries(test_bundle_writer PRIVATE Qt6::Core Qt6::Concurrent zip IrisCore)
add_test(NAME bundle.writer COMMAND test_bundle_writer WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(bundle.writer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets" TIMEOUT 60)
jah_no_display(bundle.writer)

# WHAT EACH SCRIPT ROW HERE NEEDS BUILT beyond the binary on its command line (jah_builds, tests/CMakeLists.txt): nothing.
jah_builds(none ROWS library.scale)
