set(BREAKPAD_SRCS ${BREAKPAD_SRCS}
	breakpad/src/client/windows/handler/exception_handler.cc 
	breakpad/src/client/windows/crash_generation/crash_generation_client.cc 
	breakpad/src/common/windows/guid_string.cc
	# crash sender
	breakpad/src/client/windows/sender/crash_report_sender.cc
	breakpad/src/common/windows/http_upload.cc
)

set(BREAKPAD_LIBS ${BREAKPAD_LIBS} wininet.lib)
