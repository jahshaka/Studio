# https://gist.github.com/Rod-Persky/e6b93e9ee31f9516261b
# This depends on the compiler we are using, see top level file for DestDir
if (WIN32)
get_target_property(QT5_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
get_filename_component(QT5_WINDEPLOYQT_EXECUTABLE ${QT5_QMAKE_EXECUTABLE} PATH)
set(QT5_WINDEPLOYQT_EXECUTABLE "${QT5_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${QT5_WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>)
endif(WIN32)