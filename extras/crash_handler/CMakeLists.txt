cmake_minimum_required(VERSION 3.28)
foreach(p CMP0071) # 3.10: Let AUTOMOC and AUTOUIC process GENERATED files
    if(POLICY ${p})
        cmake_policy(SET ${p} NEW)
    endif()
endforeach()
project(crash_handler)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# https://stackoverflow.com/a/37211780/996468
source_group(moc FILES ${HEADERS_moc} ${QRCS})

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_executable(crash_handler main.cpp crashreportdialog.cpp ${HEADERS_moc} ${UI_moc})
target_link_libraries(crash_handler Qt6::Widgets Qt6::Network breakpad)