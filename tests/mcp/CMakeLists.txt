# MCP server end-to-end test (CLAUDE_EDITOR_SPEC.md phase 1).
#
# mcp.e2e — spawns the REAL app binary with --mcp-port (windowed, engine
# viewport up — screenshot needs it, like scripting.e2e.full_surface) under a
# scratch HOME, then drives the HTTP endpoint as an MCP client:
# initialize handshake, tools/list (exactly 5 tools), run_script building a
# scene verified via describe_scene, screenshot decoding to a real PNG,
# undo_redo, and 401 for a missing/bad token.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)

add_executable(test_mcp_e2e test_mcp_e2e.cpp)
target_link_libraries(test_mcp_e2e PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
target_compile_definitions(test_mcp_e2e PRIVATE
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")

set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-mcp)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME mcp.e2e COMMAND test_mcp_e2e
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(mcp.e2e PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "mcp"
    TIMEOUT 240)
