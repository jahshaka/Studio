# Sourced from https://stackoverflow.com/q/697560/996468
# Copy single files
macro(copy_files files)
foreach(file ${files})
    message(STATUS "Copying resource ${file}")
    file(COPY ${file} DESTINATION ${DestDir})
endforeach()
endmacro()

# Copy full directories
macro(copy_dirs dirs)
foreach(dir ${dirs})
    # Replace / at the end of the path (copy dir content VS copy dir)
    string(REGEX REPLACE "/+$" "" dirclean "${dir}")
    message(STATUS "Copying resource ${dirclean}")
    file(COPY ${dirclean} DESTINATION ${DestDir})
endforeach()
endmacro()

# Copy Jahshaka data folders after a successful build
set(DataDirs app scenes)
foreach(dir ${DataDirs})
if (APPLE)
	add_custom_command(
		TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				${PROJECT_SOURCE_DIR}/${dir}
				${DestDir}/${APP_OUTPUT_NAME}.app/Contents/MacOS/${dir})
else()
	add_custom_command(
		TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				${PROJECT_SOURCE_DIR}/${dir}
				${DestDir}/${dir})
endif()
endforeach()

# The downloader used to be copied into Contents/MacOS/downloader.app. It was
# dead there: src/ui/dialogs/softwareupdatedialog.cpp:31 launches it from
# QDir::currentPath(), not from the bundle, and the update check itself is
# commented out in src/app/main.cpp. What it did do was add a nested application
# bundle that has to be signed, deployed and shipped. The downloader target is
# still built into bin/ — it is simply no longer embedded in the app.
