cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
foreach(p CMP0071) # 3.10: Let AUTOMOC and AUTOUIC process GENERATED files
    if(POLICY ${p})
        cmake_policy(SET ${p} NEW)
    endif()
endforeach()
project(crash_handler)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

qt5_wrap_cpp(HEADERS_moc crashreportdialog.h)
qt5_wrap_ui(UI_moc crashreportdialog.ui)

add_executable(crash_handler main.cpp crashreportdialog.cpp ${HEADERS_moc} ${UI_moc})
target_link_libraries(crash_handler Qt5::Widgets Qt5::Network breakpad)
