#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "zip::zip" for configuration "RelWithDebInfo"
set_property(TARGET zip::zip APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(zip::zip PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C;CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/zip_RelWithDebugInfo.lib"
  )

list(APPEND _cmake_import_check_targets zip::zip )
list(APPEND _cmake_import_check_files_for_zip::zip "${_IMPORT_PREFIX}/lib/zip_RelWithDebugInfo.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
