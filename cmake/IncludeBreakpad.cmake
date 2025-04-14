set(USE_BREAKPAD FALSE)

# only enable breakpad on windows platforms using msvc for now
if(WIN32 OR (UNIX AND NOT APPLE))
	set(USE_BREAKPAD TRUE)
endif()

# Finds the function from an earlier include
if(USE_BREAKPAD)
	add_subdirectory_with_folder("Extras" ${CMAKE_CURRENT_SOURCE_DIR}/extras/crash_handler)
	add_subdirectory_with_folder("Extras" ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/breakpad)

	set(HEADERS ${HEADERS} src/breakpad/breakpad.h)

	add_definitions("-DUSE_BREAKPAD")

	set_target_properties(breakpad PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
	set_target_properties(crash_handler PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()