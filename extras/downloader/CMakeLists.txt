cmake_minimum_required(VERSION 3.28)
foreach(p CMP0071) # 3.10: Let AUTOMOC and AUTOUIC process GENERATED files
    if(POLICY ${p})
        cmake_policy(SET ${p} NEW)
    endif()
endforeach()
project(downloader)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(OpenGL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

#qt5_wrap_cpp(HEADERS_moc mainwindow.h progressbar.h)
set(HEADERS mainwindow.h progressbar.h)
set(SRCS main.cpp mainwindow.cpp progressbar.cpp)

if(WIN32)
add_executable(downloader ${SRCS} ${HEADERS})
#set(CMAKE_WIN32_EXECUTABLE ON)
else()
add_executable(downloader MACOSX_BUNDLE ${SRCS} ${HEADERS})
endif(WIN32)
target_link_libraries(downloader Qt6::Widgets Qt6::Network)

#if(APPLE)
#    add_custom_command(
#        TARGET downloader POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy_directory
#            ${CMAKE_BINARY_DIR}/bin/downloader.app
#            ${CMAKE_BINARY_DIR}/bin/Jahshaka.app/Contents/MacOS/)
#endif()
