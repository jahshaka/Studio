set (MINER_ENABLED OFF CACHE BOOL "Enable Mining (OpenCL required)")
set (MINER_MHTTPD_LIB "NONE" CACHE BOOL "path to libmicrohttpd .lib file")
set (MINER_MHTTPD_INCLUDE "NONE" CACHE BOOL "path to libmicrohttpd ")
if(MINER_ENABLED)
    #add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cuda_gpu_list)

	# download xmr-stak deps and extract them
	include(ExternalProject)
	# https://stackoverflow.com/questions/39022188/how-to-download-arbitrary-files-as-part-of-a-custom-target-with-cmake
	ExternalProject_Add(
		xmr-stak-deps
		PREFIX "xmr-stak-deps"
		URL https://github.com/fireice-uk/xmr-stak-dep/releases/download/v2/xmr-stak-dep.zip
		CONFIGURE_COMMAND ""
		BUILD_COMMAND ""
		INSTALL_COMMAND ""
	)

    #XMR-STAK
	set(OpenSSL_ENABLE OFF)
	set(HWLOC_ENABLE OFF)
	set(CUDA_ENABLE OFF)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xmr-stak)
    set_target_properties(xmr-stak PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/xmr-stak)
    set_target_properties(xmr-stak PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/xmr-stak)
    set_target_properties(xmr-stak PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/xmr-stak)
    set_target_properties(xmr-stak PROPERTIES OpenSSL_ENABLE OFF)
    set_target_properties(xmr-stak PROPERTIES HWLOC_ENABLE OFF)
    set_target_properties(xmr-stak PROPERTIES CUDA_ENABLE OFF)

	#set the path for the dependencies
	

	find_package(OpenCL REQUIRED)
	#enable_language("CUDA")
	add_definitions("-DMINER_ENABLED")

	set(HEADERS ${HEADERS}
				src/miner/minerprocess.h
				src/miner/cuda_gpu_list.h
				src/miner/minerchart.h
				src/miner/minerui.h
				src/miner/mswitch.h
				src/miner/autominer.h
				)

	set(SRCS 	${SRCS}
				src/miner/minerprocess.cpp
				src/miner/minerui.cpp
				src/miner/autominer.cpp
				)

    # add cuda lib
    #set(LIBS ${LIBS} cuda_gpu_list)
    #set_target_properties(cuda_gpu_list PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

	## OPENCL
	find_path(OpenCL_INCLUDE_DIR
        NAMES
            CL/cl.h
            OpenCL/cl.h
        NO_DEFAULT_PATH
        PATHS
            ENV "OpenCL_ROOT"
            ENV AMDAPPSDKROOT
            ENV ATISTREAMSDKROOT
            ENV "PROGRAMFILES(X86)"
            /usr
        PATH_SUFFIXES
            include
            OpenCL/common/inc
            "AMD APP/include")

    find_library(OpenCL_LIBRARY
        NAMES 
            OpenCL
            OpenCL.lib
        NO_DEFAULT_PATH
        PATHS
            ENV "OpenCL_ROOT"
            ENV AMDAPPSDKROOT
            ENV ATISTREAMSDKROOT
            ENV "PROGRAMFILES(X86)"
        PATH_SUFFIXES
            "AMD APP/lib/x86_64"
            lib/x86_64
            lib/x86_64-linux-gnu
            lib/x64
            OpenCL/common/lib/x64)
    # find package will use the previews searched path variables
    find_package(OpenCL)
    if(OpenCL_FOUND)
        include_directories(SYSTEM ${OpenCL_INCLUDE_DIRS})
        set(LIBS ${LIBS} ${OpenCL_LIBRARY})
        link_directories(${OpenCL_LIBRARY})
		#target_link_libraries(${CMAKE_PROJECT_NAME} ${OpenCL_LIBRARY} )
    else()
        message(FATAL_ERROR "OpenCL NOT found: use `-DOpenCL_ENABLE=OFF` to build without OpenCL support for AMD gpu's")
    endif()

endif()