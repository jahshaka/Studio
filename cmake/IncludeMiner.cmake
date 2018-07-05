set (MINER_ENABLED OFF CACHE BOOL "Enable Mining (OpenCL required)")
if(MINER_ENABLED)
    #add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cuda_gpu_list)

    #XMR-STAK
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xmr-stak)
    set_target_properties(xmr-stak PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/xmr-stak)
    set_target_properties(xmr-stak PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/xmr-stak)
    set_target_properties(xmr-stak PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/xmr-stak)


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