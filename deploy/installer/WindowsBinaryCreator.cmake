# deploy/installer/WindowsBinaryCreator.cmake

message(STATUS "[Installer] Running Qt Installer Framework binarycreator...")

set(BINARYCREATOR_PATH "$ENV{BINARYCREATOR_PATH}" CACHE STRING "Path to binarycreator.exe")

if(NOT EXISTS "${BINARYCREATOR_PATH}")
    message(FATAL_ERROR "binarycreator not found. Please set the BINARYCREATOR_PATH env variable.")
endif()

set(INSTALLER_ROOT "${CMAKE_BINARY_DIR}/installer")
set(INSTALLER_DATA_DIR "${INSTALLER_ROOT}/packages/org.jahshaka.package/data")
set(INSTALLER_CONFIG_FILE "${INSTALLER_ROOT}/config/config.xml")
set(INSTALLER_OUTPUT "${INSTALLER_ROOT}/bin/${APP_OUTPUT_NAME}-installer.exe")

file(MAKE_DIRECTORY "${INSTALLER_DATA_DIR}")
file(MAKE_DIRECTORY "${INSTALLER_ROOT}/bin")

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${INSTALLER_DATA_DIR}/$<TARGET_FILE_NAME:${CMAKE_PROJECT_NAME}>"
    COMMENT "[Installer] Copying application to installer data directory"
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND "${BINARYCREATOR_PATH}" -c "${INSTALLER_CONFIG_FILE}" -p "${INSTALLER_ROOT}/packages" "${INSTALLER_OUTPUT}"
    COMMENT "[Installer] Creating installer with binarycreator"
)
