cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(breakpad)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(SRCS)
set(LIBS)

if (WIN32)
        include (WindowsBreakpad)
endif (WIN32)

add_library(breakpad SHARED ${SRCS})

target_include_directories(breakpad PUBLIC
    "${PROJECT_SOURCE_DIR}/breakpad/src/"
)

target_link_libraries(breakpad ${LIBS})
add_definitions("-DUNICODE")

