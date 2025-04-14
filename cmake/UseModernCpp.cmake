# sourced from https://stackoverflow.com/a/31010221/996468
# Activate C++11 in our project and allow backtracking a little just in case
macro(use_cxx11)
if (CMAKE_VERSION VERSION_LESS "3.1")
    # Could be bad? https://www.slideshare.net/DanielPfeifer1/cmake-48475415 - Slide 36
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set (CMAKE_CXX_FLAGS "-std=gnu++11 ${CMAKE_CXX_FLAGS}")
    endif ()
else ()
    set (CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED True)
endif ()

# Fix behavior of CMAKE_CXX_STANDARD when targeting macOS.
if (POLICY CMP0025)
    cmake_policy(SET CMP0025 NEW)
endif ()

endmacro(use_cxx11)