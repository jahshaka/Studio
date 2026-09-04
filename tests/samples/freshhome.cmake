# samples.freshhome.<sample>: wipe and recreate one sample's scratch HOME.
#
# The setup fixture behind samples.cleanstart.*. A clean start is the whole
# subject of that gate, and a HOME left over from the previous ctest run has the
# sample already installed — so the import step would be exercising a re-import
# and the "virgin machine" claim would be false from the second run onwards.
if(NOT DEFINED HOME_DIR)
    message(FATAL_ERROR "freshhome.cmake: -DHOME_DIR=<path> is required")
endif()
file(REMOVE_RECURSE ${HOME_DIR})
file(MAKE_DIRECTORY ${HOME_DIR}/run)
message(STATUS "fresh HOME: ${HOME_DIR}")
