cmake_minimum_required(VERSION 3.28 FATAL_ERROR)
project(breakpad)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(BREAKPAD_SRCS)
set(BREAKPAD_LIBS)

if(WIN32)
    include(WindowsBreakpad)
endif (WIN32)

if((UNIX) AND (NOT APPLE))
    include(LinuxBreakpad)
endif()

add_library(breakpad STATIC ${BREAKPAD_SRCS})

target_include_directories(breakpad PUBLIC "${PROJECT_SOURCE_DIR}/breakpad/src/")

target_link_libraries(breakpad ${BREAKPAD_LIBS})
add_definitions("-DUNICODE")

