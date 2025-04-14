# https://gist.github.com/Rod-Persky/e6b93e9ee31f9516261b
# This depends on the compiler we are using, see top level file for DestDir
if (WIN32)
    get_filename_component(QT6_WINDEPLOYQT_EXECUTABLE "${QT_QMAKE_EXECUTABLE}" DIRECTORY) # Use QT_QMAKE_EXECUTABLE
    set(QT6_WINDEPLOYQT_EXECUTABLE "${QT6_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")

    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${QT6_WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}> -multimedia)
endif(WIN32)