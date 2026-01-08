#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "zip::zip" for configuration "Release"
set_property(TARGET zip::zip APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(zip::zip PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C;CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/zip.lib"
  )

list(APPEND _cmake_import_check_targets zip::zip )
list(APPEND _cmake_import_check_files_for_zip::zip "${_IMPORT_PREFIX}/lib/zip.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
