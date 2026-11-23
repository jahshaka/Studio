# db.layer — the library's SQLite layer (src/data/db/, NOQT-DATABASE-1 stage 1) on a throwaway file of its
# own: typed round trips (NULL vs '' vs x''), binds, the named errors, the transaction guard, two
# connections' lock with its holder named, a crash mid-transaction, the version floor. CPU, seconds.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_db_layer test_db_layer.cpp)
target_link_libraries(test_db_layer PRIVATE jahshaka_db Qt6::Core)
add_test(NAME db.layer COMMAND test_db_layer WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(db.layer PROPERTIES TIMEOUT 60 LABELS "db")
jah_no_display(db.layer)

# db.units — the REAL Database's units of writes on the layer (NOQT-DATABASE-1 stage 2, the transaction core): the
# borrow's refusal, a failed BEGIN's refusal with its holder named, the batch's stand-down, a dropped unit's dump,
# a process killed mid-unit. database.cpp's link closure (as tests/desktops). CPU, seconds.
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)
add_executable(test_db_units test_db_units.cpp
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
    # the legacy log lines are the session log's (JahLog: a leaf service + its settings reader; NOQT-CORE-1 E2)
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_db_units PROPERTIES AUTOMOC ON)
target_include_directories(test_db_units PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_db_units PRIVATE jahshaka_db IrisGL Qt6::Widgets Qt6::Multimedia)
add_test(NAME db.units COMMAND test_db_units WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(db.units PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "db")
jah_no_display(db.units)

# db.layer_guard — A FAILED READ NEVER DECIDES A WRITE UNSEEN (NOQT-DATABASE-1 stage 3, the lead's ruling): the layer's
# guard in db::Connection (connection.h, THE LAYER GUARD) - the mark of an unlooked failed read, the refusal by name of
# the next write (consumed by a bare write, held by a unit - which fails whole - and by a script run's gesture batch to
# its end), a pending library's refusal; the injection is test-side (tests/db/dbseam.h). database.cpp's link closure
# (as db.units) for the batch case. CPU, seconds.
add_executable(test_layer_guard test_layer_guard.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_layer_guard PROPERTIES AUTOMOC ON)
target_include_directories(test_layer_guard PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_layer_guard PRIVATE jahshaka_db IrisGL Qt6::Widgets Qt6::Multimedia)
add_test(NAME db.layer_guard COMMAND test_layer_guard WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(db.layer_guard PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "db")
jah_no_display(db.layer_guard)

# db.failed_opens — A FAILED READ AT AN OPEN REFUSES THE OPEN BY NAME (NOQT-DATABASE-1 stage 2, the audit's findings
# 6 and 7): the startup generation check never resets a library whose user_version could not be read, and a project
# open never opens an empty world from a scene read that failed. The REAL checkAndWipe, readPlan and the reset's
# catalog steps (libraryresetkeep.cpp: the wipe, the rebuild, the store's sweep, the stamp) on throwaway files and
# stores; the reset's file steps and its bootstrap, and SceneReader's source collectors, are recorders in the test
# (its header says why).
# database.cpp's link closure with the session rows' (as tests/materialbundle). CPU, seconds.
add_executable(test_failed_opens test_failed_opens.cpp
    ${CMAKE_SOURCE_DIR}/src/services/librarygeneration.cpp
    ${CMAKE_SOURCE_DIR}/src/services/libraryresetkeep.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmigration.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetgc.cpp
    ${CMAKE_SOURCE_DIR}/src/services/openreads.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstore.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/projectassets.cpp
    ${CMAKE_SOURCE_DIR}/src/services/materialbundle.cpp
    ${CMAKE_SOURCE_DIR}/src/services/materialmembers.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetdelete.cpp
    ${CMAKE_SOURCE_DIR}/src/services/memberstamp.cpp
    ${CMAKE_SOURCE_DIR}/src/services/projectmembership.cpp
    ${CMAKE_SOURCE_DIR}/src/services/imagematerial.cpp
    ${CMAKE_SOURCE_DIR}/src/services/meshbakestore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/primitiveassets.cpp
    ${CMAKE_SOURCE_DIR}/src/services/loadtimeline.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_failed_opens PROPERTIES AUTOMOC ON)
target_include_directories(test_failed_opens PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_failed_opens PRIVATE jahshaka_db IrisGL Qt6::Widgets Qt6::Multimedia)
add_test(NAME db.failed_opens COMMAND test_failed_opens WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(db.failed_opens PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "db")
jah_no_display(db.failed_opens)
