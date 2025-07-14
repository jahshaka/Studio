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

