# PREVIEWS-1 — the preview service's CPU rows (no engine, no display).

# previews.key — the preview key moves with what the picture depicts and with nothing else: each
# input alone (a definition oid, a member oid, the project pin, the preset, previewenv's version for a
# studio-lit preset only), equal under a rename and any oid order, the guid only for a row with no
# content; negative control a name-keyed key. The pure function: services/previewkey.cpp alone.
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
add_executable(test_preview_writer test_preview_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/services/previewwriter.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    # database.cpp's link closure (the set tests/desktops links)
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
set_target_properties(test_preview_writer PROPERTIES AUTOMOC ON CXX_STANDARD 17)
target_include_directories(test_preview_writer PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_preview_writer PRIVATE IrisGL Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME previews.writer COMMAND test_preview_writer WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(previews.writer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "previews" TIMEOUT 60)
jah_no_display(previews.writer)
