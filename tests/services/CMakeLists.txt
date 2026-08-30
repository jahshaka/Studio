# Service-layer unit tests (APP_ARCHITECTURE_AUDIT §9 Phase 1).
#
# services.core — links ONLY the QObject-free/minimally-Qt services it tests
# (UndoService + SelectionService), no MainWindow, no engine, no document
# construction (SelectionService is exercised through null SceneNodePtrs; the
# guard and re-emit contracts do not depend on a live node). Runs offscreen.
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets)
add_executable(test_services test_services.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/selectionservice.cpp)
set_target_properties(test_services PROPERTIES AUTOMOC ON)
target_include_directories(test_services PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include)
target_link_libraries(test_services PRIVATE Qt6::Gui Qt6::Widgets)
add_test(NAME services.core COMMAND test_services
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")
