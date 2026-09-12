# assets.tray — the EDITOR TRAY's listing and the Assets page's LIBRARY GRID,
# as rules AND as queries (small-items round B). Builds the REAL Database,
# services/assettray.cpp and services/imagematerial.cpp against a throwaway
# SQLite file with a realistic library in it: the five collapse rules, the
# grid's membership rule, the project import's parent remap, and a COUNT of
# the SQL statements one listing (and one search keystroke) executes — the
# tray runs on every edge write and every keypress, so its cost is per-key.
# Headless (offscreen platform); never touches the user's live JahLibrary.db.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
add_executable(test_assettray test_assettray.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assettray.cpp
    ${CMAKE_SOURCE_DIR}/src/services/imagematerial.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    # database.cpp's link closure (same as tests/drawers)
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
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
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_assettray PROPERTIES AUTOMOC ON)
target_include_directories(test_assettray PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_assettray PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia)

# THE STATEMENT COUNT comes from sqlite3_trace_v2 on the handle Qt's own
# QSQLITE driver opened (QSqlDriver::handle) — no instrumentation in the
# production path. Where the header/library is absent the suite still runs and
# still checks every RULE; it just does not assert the counts.
find_path(JAH_SQLITE3_INCLUDE sqlite3.h)
find_library(JAH_SQLITE3_LIB sqlite3)
if (JAH_SQLITE3_INCLUDE AND JAH_SQLITE3_LIB)
    target_compile_definitions(test_assettray PRIVATE JAH_HAVE_SQLITE3)
    target_include_directories(test_assettray PRIVATE ${JAH_SQLITE3_INCLUDE})
    target_link_libraries(test_assettray PRIVATE ${JAH_SQLITE3_LIB})
else()
    message(STATUS "assets.tray: no sqlite3 headers — the query-count bound is not asserted")
endif()

add_test(NAME assets.tray COMMAND test_assettray
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(assets.tray PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets")

jah_lsan_lane(assets.tray)
jah_no_display(assets.tray)
