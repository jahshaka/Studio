# deploy/installer/LinuxBinaryCreator.cmake

message(STATUS "[Installer] Running binarycreator...")

set(BINARYCREATOR_PATH "$ENV{BINARYCREATOR_PATH}" CACHE STRING "Path to binarycreator(.run)")

if(NOT EXISTS "${BINARYCREATOR_PATH}")
    if(WIN32)
        find_program(BINARYCREATOR_PATH NAMES binarycreator.exe)
    else()
        find_program(BINARYCREATOR_PATH NAMES binarycreator)
    endif()
endif()

if(NOT EXISTS "${BINARYCREATOR_PATH}")
    if(WIN32)
        file(GLOB _qtifw_paths
            "$ENV{USERPROFILE}/Qt/Tools/QtInstallerFramework/*/bin/binarycreator"
        )
    else()
        file(GLOB _qtifw_paths
            "$ENV{HOME}/Qt/Tools/QtInstallerFramework/*/bin/binarycreator"
        )
    endif()

    list(LENGTH _qtifw_paths _len)
    if(_len GREATER 0)
        list(GET _qtifw_paths 0 BINARYCREATOR_PATH)
    endif()
endif()

if(NOT BINARYCREATOR_PATH OR NOT EXISTS "${BINARYCREATOR_PATH}")
    message(FATAL_ERROR "binarycreator not found. Please install Qt Installer Framework or set BINARYCREATOR_PATH.")
endif()

message(STATUS "Using binarycreator at: ${BINARYCREATOR_PATH}")

set(APP_OUTPUT_NAME "${CMAKE_PROJECT_NAME}")
set(INSTALLER_ROOT "${CMAKE_BINARY_DIR}/installer")
set(INSTALLER_CONFIG_FILE "${INSTALLER_ROOT}/config/linux.xml")
set(INSTALLER_OUTPUT "${INSTALLER_ROOT}/bin/${APP_OUTPUT_NAME}-installer.run")

file(MAKE_DIRECTORY "${INSTALLER_ROOT}/bin")

add_custom_target(create_installer
    COMMAND "${BINARYCREATOR_PATH}"
            -c "${INSTALLER_CONFIG_FILE}"
            -p "${INSTALLER_ROOT}/packages"
            "${INSTALLER_OUTPUT}"
    DEPENDS copy_deploy_to_installer_data
    COMMENT "[Installer] Creating installer..."
)

