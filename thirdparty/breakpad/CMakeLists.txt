project(breakpad)

set(SRCS
	breakpad/src/client/windows/handler/exception_handler.cc 
	breakpad/src/client/windows/crash_generation/crash_generation_client.cc 
	breakpad/src/common/windows/guid_string.cc

	#crash sender
	breakpad/src/client/windows/sender/crash_report_sender.cc
	breakpad/src/common/windows/http_upload.cc
)

add_definitions("-DUNICODE")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)

add_library(breakpad SHARED ${SRCS})

target_include_directories(breakpad PUBLIC
    "${PROJECT_SOURCE_DIR}/breakpad/src/"
)

target_link_libraries(breakpad wininet.lib)