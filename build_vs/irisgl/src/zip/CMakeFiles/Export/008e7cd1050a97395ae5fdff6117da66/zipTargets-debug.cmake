#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "zip::zip" for configuration "Debug"
set_property(TARGET zip::zip APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(zip::zip PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C;CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/zipd.lib"
  )

list(APPEND _cmake_import_check_targets zip::zip )
list(APPEND _cmake_import_check_files_for_zip::zip "${_IMPORT_PREFIX}/lib/zipd.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
