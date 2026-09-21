# THE BOOT RE-STAMP OF THE SHIPPED PRESETS' MAPS (SEED-RESTAMP-1), headless,
# against the REAL Database + the REAL CAS on a throwaway SQLite file and store
# root. The owner's #56 finding: a library seeded before SEED-STAMP-1 keeps its
# preset maps unstamped for ever, so they stand in the tray as loose tiles.
# Asserts the repair's whole rule — matched by CONTENT (the duplicate row over
# one map's bytes included), the user's own picture untouched, the maps folded,
# no pin moved, idempotent, the per-preset gate that leaves a stamping seed's
# batch alone, and the pre-gate that answers a healthy library in one query.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
add_executable(test_preset_restamp test_preset_restamp.cpp
    ${CMAKE_SOURCE_DIR}/src/services/presetrestamp.cpp
    ${CMAKE_SOURCE_DIR}/src/services/materialbundle.cpp
    ${CMAKE_SOURCE_DIR}/src/services/materialmembers.cpp
    ${CMAKE_SOURCE_DIR}/src/services/memberstamp.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetdelete.cpp
    ${CMAKE_SOURCE_DIR}/src/services/projectmembership.cpp
    ${CMAKE_SOURCE_DIR}/src/services/imagematerial.cpp
    ${CMAKE_SOURCE_DIR}/src/services/projectassets.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/meshbakestore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/loadtimeline.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_preset_restamp PROPERTIES AUTOMOC ON)
target_include_directories(test_preset_restamp PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_preset_restamp PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME assets.seed_restamp COMMAND test_preset_restamp
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(assets.seed_restamp PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets")
jah_no_display(assets.seed_restamp)
