# THE MATERIAL BUNDLE MODEL (MATERIAL_BUNDLE_SPEC phase 1), headless, against
# the REAL Database + the REAL CAS on a throwaway SQLite file and store root.
# Asserts the model itself rather than any UI: the definition is the row's CAS
# `source` file (D-2), membership edges are DERIVED from it and carry NO
# project stamp (audit G2), a path in a texture slot is REFUSED (F3), a
# project-scope save moves only that project's pin (F11 / the owner's Q2
# model), and a graph rides the definition as a payload with no second row.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
add_executable(test_material_bundle test_material_bundle.cpp
    ${CMAKE_SOURCE_DIR}/src/services/materialbundle.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetdelete.cpp
    ${CMAKE_SOURCE_DIR}/src/services/memberstamp.cpp
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
set_target_properties(test_material_bundle PROPERTIES AUTOMOC ON)
target_include_directories(test_material_bundle PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_material_bundle PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME materials.bundle COMMAND test_material_bundle
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(materials.bundle PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets")
jah_no_display(materials.bundle)
