# The session log (SPECS/SESSION_LOG_SPEC.md).
#
# log.core — the sink itself: levels, categories, the precedence chain, the
# file lifecycle, rotation, the Qt funnel and the §8-R5 micro-benchmark. Links
# ONLY JahLog + SessionHeader + the version constants (the services.core shape):
# no MainWindow, no engine, no document, no display. It re-execs ITSELF for the
# scenarios that need a real session file, because JahLog::start() is
# once-per-process by design.
#
# IrisGL is linked for one symbol: iris::Logger, which JahLog forwards into
# (fork F5-A, absorb at the sink). Nothing else in the document library is
# touched, and no engine boots.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
add_executable(test_jahlog test_jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/services/sessionheader.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_jahlog PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_jahlog PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl)
# The same build id the shader-cache fingerprint uses — the header row that
# correlates a log with a cache directory has to be the real one here too.
target_compile_definitions(test_jahlog PRIVATE
    GIT_COMMIT_HASH="${GIT_COMMIT_HASH}" GIT_COMMIT_DATE="${GIT_COMMIT_DATE}"
    JAHSHAKA_VERSION="${JAHSHAKA_VERSION}")
target_link_libraries(test_jahlog PRIVATE IrisGL Qt6::Core Qt6::Gui)
add_test(NAME log.core COMMAND test_jahlog WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(log.core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 300 LABELS "log")
# No display at all: the whole point of the services.core shape.
jah_no_display(log.core)

