cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(crash_handler)

set (CMAKE_CXX_STANDARD 11)
set (CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Qt5 REQUIRED COMPONENTS Concurrent Widgets Sql Svg Network)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(SRCS crashreportdialog.cpp)
set(HEADERS crashreportdialog.h)
set(FORMS crashreportdialog.ui)

qt5_wrap_cpp(HEADERS_moc ${HEADERS})
qt5_wrap_ui(UI_moc ${FORMS})

add_executable(crash_handler main.cpp ${SRCS} ${HEADERS_moc})
target_link_libraries(crash_handler Qt5::Concurrent Qt5::Sql Qt5::Svg Qt5::Widgets Qt5::Network breakpad)
