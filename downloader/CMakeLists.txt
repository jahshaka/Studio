cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(downloader)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

qt5_wrap_cpp(HEADERS_moc mainwindow.h)
qt5_wrap_ui(UI_moc mainwindow.ui)

add_executable(downloader main.cpp mainwindow.cpp ${HEADERS_moc} ${UI_moc})
target_link_libraries(downloader Qt5::Widgets Qt5::Network)
