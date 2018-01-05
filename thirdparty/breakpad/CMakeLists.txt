project(breakpad)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if (WIN32)
	include (WindowsBreakpad)
endif (WIN32)