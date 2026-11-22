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
