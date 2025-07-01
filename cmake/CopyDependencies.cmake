# https://gist.github.com/Rod-Persky/e6b93e9ee31f9516261b
# This depends on the compiler we are using, see top level file for DestDir
if (WIN32)
    find_program(QT6_WINDEPLOYQT_EXECUTABLE windeployqt)

    if(NOT QT6_WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "windeployqt not found in PATH. Please make sure Qt/bin is in your system PATH.")
    endif()

    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND "${QT6_WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" -multimedia
        COMMENT "Running windeployqt from PATH"
    )
endif(WIN32)
