# shell.contract — the module contract as the shell executes it (D10-SHELL-MODULES):
# a fake StudioModule driven through the shell's real ModuleHub, PageHost and
# ActionHost (ShortcutRegistry + toolbar slots + menus). No window shown, no
# engine, no database — links only the parts under test.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(test_shell_contract test_shell_contract.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/actionhost.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/modulehub.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/pagehost.cpp
    ${CMAKE_SOURCE_DIR}/src/services/shortcutregistry.cpp)
set_target_properties(test_shell_contract PROPERTIES AUTOMOC ON CXX_STANDARD 17)
target_include_directories(test_shell_contract PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_shell_contract PRIVATE Qt6::Widgets)
add_test(NAME shell.contract COMMAND test_shell_contract
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(shell.contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")
jah_no_display(shell.contract)
