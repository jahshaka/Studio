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
