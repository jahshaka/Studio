# PREVIEWS-1 — the preview service's CPU rows (no engine, no display).

# previews.key — the preview key moves with what the picture depicts and with nothing else: each
# input alone (a definition oid, a member oid, the project pin, the preset, previewenv's version for a
# studio-lit preset only), equal under any oid order (the rename law: scripting.e2e.previews_state), the guid only for a row with no
# content. The pure function: services/previewkey.cpp alone.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_preview_key test_preview_key.cpp ${CMAKE_SOURCE_DIR}/src/services/previewkey.cpp)
set_target_properties(test_preview_key PROPERTIES CXX_STANDARD 17 AUTOMOC OFF)
target_include_directories(test_preview_key PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_preview_key PRIVATE Qt6::Core)
add_test(NAME previews.key COMMAND test_preview_key WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(previews.key PROPERTIES LABELS "previews" TIMEOUT 60)
jah_no_display(previews.key)

# previews.writer — THE ONE PREVIEW WRITER (services/previewwriter.cpp) on a temp library (the real Database, its own
# file): bytes and key in one transaction; a null image or an empty PNG changes nothing (negative control: the unguarded
# updateAssetThumbnail blanks the tile); a write submitted under the UI's write lock returns at once and lands after it;
# submit order; past a (short) budget a write is dropped, reported and re-queued at the head; the UI connection's busy
# timeout read; the writer's end drains. No engine, no display.
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Sql Multimedia Concurrent)
set(_previews_db_closure
    ${CMAKE_SOURCE_DIR}/src/services/previewwriter.cpp
    ${CMAKE_SOURCE_DIR}/src/services/previewkey.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    # database.cpp's link closure (the set tests/desktops links; the statement trace LARGE-LIBRARY-1 added)
    ${CMAKE_SOURCE_DIR}/src/data/database/databasereads.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
add_executable(test_preview_writer test_preview_writer.cpp ${_previews_db_closure})
set_target_properties(test_preview_writer PROPERTIES AUTOMOC ON CXX_STANDARD 17)
target_include_directories(test_preview_writer PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_preview_writer PRIVATE IrisGL Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME previews.writer COMMAND test_preview_writer WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(previews.writer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "previews" TIMEOUT 60)
jah_no_display(previews.writer)

# previews.ui_writes — A UI WRITE NEVER FAILS BECAUSE A PREVIEW WAS WRITTEN (the lead's G1-G3): a DbTransaction that reads
# first, the script run's batch that reads first, the import claim's statement sequence (live reads: the negative
# control; scoped: green) and a transaction behind a stale live read failing LOUDLY. The real Database and writer on a
# temp library; red first on deferred BEGINs (spikes/previews-1/stage1/ui_writes-red.txt).
add_executable(test_ui_writes test_ui_writes.cpp ${_previews_db_closure})
set_target_properties(test_ui_writes PROPERTIES AUTOMOC ON CXX_STANDARD 17)
target_include_directories(test_ui_writes PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_ui_writes PRIVATE IrisGL Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME previews.ui_writes COMMAND test_ui_writes WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(previews.ui_writes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "previews" TIMEOUT 60)
jah_no_display(previews.ui_writes)
