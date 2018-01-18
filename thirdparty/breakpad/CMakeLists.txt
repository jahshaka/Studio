cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(breakpad)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set (CMAKE_CXX_STANDARD 11)
set (CMAKE_CXX_STANDARD_REQUIRED True)

set(SRCS)
set(LIBS)

if (WIN32)
        include (WindowsBreakpad)
endif (WIN32)

if ((UNIX) AND (NOT APPLE))
    include (LinuxBreakpad)
endif()

# This will allow a .lib to be built alongside the dll, ((needed)) for MSVC
if(WIN32 AND MSVC)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)
endif()

add_library(breakpad SHARED ${SRCS})

target_include_directories(breakpad PUBLIC
    "${PROJECT_SOURCE_DIR}/breakpad/src/"
)

target_link_libraries(breakpad ${LIBS})
add_definitions("-DUNICODE")

