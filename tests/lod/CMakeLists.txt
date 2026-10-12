# lod.padding_untouched — LodStrategy::lodSet never writes a padding slot (LOD-RACE-1;
# testing/defects.json TSAN-LOD-UPDATE-RACE). A padding slot of an ObjectMemoryManager
# pack (the tail of a render queue's last pack, a hole a destroyed object left) names
# the manager's ONE shared dummy NullEntity as its owner; every updateAllLodsThread
# worker whose range held one wrote that dummy's LOD bytes. The row builds a render
# queue with a hole and a tail, runs lodSet over every pack with and without the
# hysteresis band, and asserts the dummy's bytes untouched and every real object
# levelled (the header of test_lod_padding.cpp says what each check proves).
#
# Pure OgreMain: no engine, no display, no GPU, no Root — so it runs display-free
# (jah_no_display) and in the leak lane. Sub-second: ctest's documented default
# timeout (THE TIMEOUT POLICY, tests/CMakeLists.txt) is its budget.
add_executable(test_lod_padding test_lod_padding.cpp)
set_target_properties(test_lod_padding PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_lod_padding PRIVATE OgreNext)
add_test(NAME lod.padding_untouched COMMAND test_lod_padding
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lod.padding_untouched PROPERTIES LABELS "lod")
jah_no_display(lod.padding_untouched)
jah_lsan_lane(lod.padding_untouched)
