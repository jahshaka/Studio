if (WIN32)
    find_program(QT6_WINDEPLOYQT_EXECUTABLE windeployqt)
    if(NOT QT6_WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "windeployqt not found in PATH. Please make sure Qt/bin is in your system PATH.")
    endif()

    set(DEPLOY_STAMP "${CMAKE_BINARY_DIR}/deploy.stamp")

    add_custom_command(
        OUTPUT "${DEPLOY_STAMP}"
        COMMAND "${QT6_WINDEPLOYQT_EXECUTABLE}"
                "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                --dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                -quick -multimedia
        COMMAND ${CMAKE_COMMAND} -E touch "${DEPLOY_STAMP}"
        DEPENDS ${CMAKE_PROJECT_NAME}
        COMMENT "[Deploy] Running windeployqt..."
    )

    add_custom_target(deploy_app ALL DEPENDS "${DEPLOY_STAMP}")
endif()

if (UNIX)
    set(DEPLOY_STAMP "${CMAKE_BINARY_DIR}/deploy.stamp")
    get_filename_component(QT6_BASE_DIR "${Qt6Core_DIR}/../../../" ABSOLUTE)
    message(STATUS "---------------------------Qt6 base path: ${QT6_BASE_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    # Add a custom target that depends on the executable
    add_custom_command(
        OUTPUT "${DEPLOY_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E echo "Deploying Jahshaka and dependencies..."
        COMMAND bash "${CMAKE_SOURCE_DIR}/deploy/data/linux/copy_linux_deps.sh"
                "$<TARGET_FILE:Jahshaka>"          # executable
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" # destination
                "${QT6_BASE_DIR}"                  # Qt6 base
        COMMAND ${CMAKE_COMMAND} -E touch "${DEPLOY_STAMP}"
        DEPENDS ${CMAKE_PROJECT_NAME}
        COMMENT "Copying all runtime dependencies after build"
    )

    add_custom_target(deploy_app ALL DEPENDS "${DEPLOY_STAMP}")
endif()


