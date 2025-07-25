# deploy/installer/config.cmake

message(STATUS "[Installer] Generating Installer Framework configuration files...")

if(WIN32)
    set(RootDir "@RootDir@")
    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/config/windows.xml.in
        ${CMAKE_BINARY_DIR}/installer/config/windows.xml
    )
    include(${CMAKE_CURRENT_LIST_DIR}/WindowsBinaryCreator.cmake)
endif()

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/packages/org.jahshaka.package/meta/package.xml.in
    ${CMAKE_BINARY_DIR}/installer/packages/org.jahshaka.package/meta/package.xml
)

file(COPY
    ${CMAKE_CURRENT_LIST_DIR}/packages/org.jahshaka.package/meta/componentscript.js
    DESTINATION
    ${CMAKE_BINARY_DIR}/installer/packages/org.jahshaka.package/meta/
)

file(COPY
    ${CMAKE_CURRENT_LIST_DIR}/config/controlscript.js
    DESTINATION
    ${CMAKE_BINARY_DIR}/installer/config/
)

set(INSTALLER_DATA_DIR "${CMAKE_BINARY_DIR}/installer/packages/org.jahshaka.package/data")

add_custom_command(
    OUTPUT "${INSTALLER_DATA_DIR}/.copied"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            "${INSTALLER_DATA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${INSTALLER_DATA_DIR}/.copied"
    DEPENDS "${DEPLOY_STAMP}"
    COMMENT "[Installer] Copying deployed files to installer data directory..."
)

add_custom_target(copy_deploy_to_installer_data ALL
    DEPENDS "${INSTALLER_DATA_DIR}/.copied"
)

