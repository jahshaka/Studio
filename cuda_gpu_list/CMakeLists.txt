cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(cuda_gpu_list)

set_directory_properties( PROPERTIES COMPILE_DEFINITIONS "" )
enable_language("CUDA")
set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -std=c++11")
set(CUDA_HOST_COMPILER ${CMAKE_CXX_COMPILER} CACHE FILEPATH "Host side compiler used by NVCC" FORCE)
set(CUDA_COMPILER "nvcc")

set(HEADERS
			cuda_gpu_list.h
			)

set(SRCS
			cuda_gpu_list.cu
			)

add_library(cuda_gpu_list SHARED ${SRCS} ${HEADERS})