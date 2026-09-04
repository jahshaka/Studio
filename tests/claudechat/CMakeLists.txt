# Claude chat (CLAUDE_EDITOR_SPEC phases 2+3): the launch-config generator
# (MCP wiring + skill install + lockdown argv + session persistence), the
# stream-json parser over fixture transcripts (one captured from the real
# CLI), the CLI probe, the subprocess host against a fake stream-json claude,
# and the floating window's states/geometry — all offscreen. The real-CLI
# end-to-end is a manual test (needs a logged-in Claude Code).
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(test_claude_chat test_claude_chat.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/claude/claudecliprobe.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/claude/claudelaunchconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/claude/claudestreamparser.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/claude/claudechathost.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/windows/claudechatwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/claude/claudeskills.qrc)
set_target_properties(test_claude_chat PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 17)
target_include_directories(test_claude_chat PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_claude_chat PRIVATE Qt6::Widgets)
add_test(NAME claude.chat COMMAND test_claude_chat
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(claude.chat PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CLAUDECHAT_FIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    LABELS "scripting")

# The leak lane (scripts/sanitize.sh lsan).
jah_lsan_lane(claude.chat)
