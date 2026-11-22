# THE DISPLAY GUARD IN EVERY TEST BINARY (DEV_ERRORS 37 + 38, 2026-11-22; tests/support/display_guard.cpp says what it does).
# A test binary run DIRECTLY from a shell inherits that shell's DISPLAY / Wayland session - on an agent's shell the OWNER's.
# The guard is not a line a suite adds (a rule kept by memory failed twice in one night): the walk below links it into EVERY
# executable target defined under tests/, at the end of tests/CMakeLists.txt, so a target in a new subdirectory is guarded
# with no line of its own. source.display_guard_linked holds every binary ctest runs to it, and proves the walk on a scratch
# project's new subdirectory.
#   jah_display_guard_library()      the OBJECT library jah_display_guard (compiled once; its object is linked whole, so the
#                                    guard's constructor runs in every binary that links it)
#   jah_display_guard_all(<dir>)     every EXECUTABLE target of <dir> and its subdirectories links it - except the ONE
#                                    target with the property JAH_DISPLAY_GUARD_CONTROL (source.display_guard_toy's negative
#                                    control, the unguarded probe; source.display_guard_linked allows that name only)
set(JAH_DISPLAY_GUARD_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../support/display_guard.cpp")
function(jah_display_guard_library)
    add_library(jah_display_guard OBJECT "${JAH_DISPLAY_GUARD_SOURCE}")
    set_target_properties(jah_display_guard PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
endfunction()
function(jah_display_guard_all _dir)
    get_property(_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        get_target_property(_control ${_t} JAH_DISPLAY_GUARD_CONTROL)
        if(_type STREQUAL "EXECUTABLE" AND NOT _control)
            target_link_libraries(${_t} PRIVATE jah_display_guard)
        endif()
    endforeach()
    get_property(_subs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subs)
        jah_display_guard_all("${_sub}")
    endforeach()
endfunction()
