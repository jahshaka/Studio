# MCP server end-to-end test (CLAUDE_EDITOR_SPEC.md phase 1).
#
# mcp.e2e — spawns the REAL app binary with --mcp-port (windowed, engine
# viewport up — screenshot needs it, like scripting.e2e.full_surface) under a
# scratch HOME, then drives the HTTP endpoint as an MCP client:
# initialize handshake, tools/list (exactly 6 tools), run_script building a
# scene verified via describe_scene, screenshot decoding to a real PNG,
# browse_assets returning captioned thumbnail images inside its budget,
# api_docs verb/search, run_script's per-run engineErrors echo, undo_redo, and
# 401 for a missing/bad token.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)

# fixtures/truncated.png is the first 60 bytes of a real PNG: signature and
# IHDR intact, no pixel data. It has to be a file the DOCUMENT accepts and the
# ENGINE refuses, which is a narrow target —
#   * a text file named .png is rejected by Texture2D::load's QImageReader
#     header probe, so it never reaches the engine at all; and
#   * Ogre's FreeImage codec reads ~90 formats (xpm, webp, psd, raw…), so an
#     exotic-but-valid image is not refused either.
# A truncated PNG passes the header probe and fails the decode — which is also
# exactly what real bit-rot looks like, and precisely the class of failure that
# produces a wrong picture and a perfectly successful script (the engine
# refuses rather than throws). That is what run_script's engineErrors echo
# exists to surface (AI_SURFACE_PROGRAM_SPEC lane C #5).
set(_bad_texture ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/truncated.png)

add_executable(test_mcp_e2e test_mcp_e2e.cpp)
target_link_libraries(test_mcp_e2e PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
target_compile_definitions(test_mcp_e2e PRIVATE
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
    JAHSHAKA_FIXTURES="${CMAKE_SOURCE_DIR}/tests/scripting/fixtures"
    JAHSHAKA_BAD_TEXTURE="${_bad_texture}")

set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-mcp)
file(MAKE_DIRECTORY ${_home}/run)
# A FRESH home per run. The home used to persist across ctest invocations and
# accumulate ~15 assets per run; after enough gates the browse_assets row this
# suite asserts on fell below the tool's 12-row default and the suite went
# red on nothing (found by the 2026-09-05 gate). The setup fixture wipes it.
add_test(NAME mcp.e2e.fresh_home
         COMMAND sh -c "rm -rf '${_home}' && mkdir -p '${_home}/run'")
set_tests_properties(mcp.e2e.fresh_home PROPERTIES
    FIXTURES_SETUP mcpFreshHome LABELS "mcp")
add_test(NAME mcp.e2e COMMAND test_mcp_e2e
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(mcp.e2e PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    FIXTURES_REQUIRED mcpFreshHome
    LABELS "mcp"
    TIMEOUT 240)
