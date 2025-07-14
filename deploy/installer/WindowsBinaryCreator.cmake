# deploy/installer/WindowsBinaryCreator.cmake

message(STATUS "[Installer] Running binarycreator...")

set(BINARYCREATOR_PATH "$ENV{BINARYCREATOR_PATH}" CACHE STRING "Path to binarycreator.exe")
if(NOT EXISTS "${BINARYCREATOR_PATH}")
    message(FATAL_ERROR "binarycreator not found. Set BINARYCREATOR_PATH.")
endif()

set(APP_OUTPUT_NAME "${CMAKE_PROJECT_NAME}")
set(INSTALLER_ROOT "${CMAKE_BINARY_DIR}/installer")
set(INSTALLER_CONFIG_FILE "${INSTALLER_ROOT}/config/windows.xml")
set(INSTALLER_OUTPUT "${INSTALLER_ROOT}/bin/${APP_OUTPUT_NAME}-installer.exe")

file(MAKE_DIRECTORY "${INSTALLER_ROOT}/bin")

# add_custom_target(create_installer
#     COMMAND "${BINARYCREATOR_PATH}"
#             -c "${INSTALLER_CONFIG_FILE}"
#             -p "${INSTALLER_ROOT}/packages"
#             "${INSTALLER_OUTPUT}"
#     DEPENDS ${CMAKE_PROJECT_NAME}
#     COMMENT "[Installer] Creating installer with binarycreator"
# )


add_custom_target(create_installer
    COMMAND "${BINARYCREATOR_PATH}"
            -c "${INSTALLER_CONFIG_FILE}"
            -p "${INSTALLER_ROOT}/packages"
            "${INSTALLER_OUTPUT}"
    DEPENDS copy_deploy_to_installer_data
    COMMENT "[Installer] Creating installer..."
)

