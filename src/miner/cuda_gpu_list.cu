#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cuda.h>
#include <cuda_runtime.h>
#include <iostream>
#include "cuda_gpu_list.h"

/*
typedef struct {
	int device_id;
	const char *device_name;
	int device_arch[2];
	int device_mpcount;
	int device_blocks;
	int device_threads;
	int device_bfactor;
	int device_bsleep;
	int syncMode;

	uint32_t *d_input;
	uint32_t inputlen;
	uint32_t *d_result_count;
	uint32_t *d_result_nonce;
	uint32_t *d_long_state;
	uint32_t *d_ctx_state;
	uint32_t *d_ctx_a;
	uint32_t *d_ctx_b;
	uint32_t *d_ctx_key1;
	uint32_t *d_ctx_key2;
	uint32_t *d_ctx_text;
	std::string name;
	size_t free_device_memory;
	size_t total_device_memory;
} nvid_ctx;
*/
/** execute and check a CUDA api command
*
* @param id gpu id (thread id)
* @param msg message string which should be added to the error message
* @param ... CUDA api command
*/
#define CUDA_CHECK_MSG(id, msg, ...) { \
	cudaError_t error = __VA_ARGS__; \
	if(error!=cudaSuccess){	\
		std::cerr << "[CUDA] Error gpu " << id << ": <" << __FILE__ << ">:" << __LINE__; \
		std::cerr << msg << std::endl;                                         \
		throw std::runtime_error(std::string("[CUDA] Error: ") + std::string(cudaGetErrorString(error))); \
	} \
} \
( (void) 0 )

/** execute and check a CUDA api command
*
* @param id gpu id (thread id)
* @param ... CUDA api command
*/
#define CUDA_CHECK(id, ...) CUDA_CHECK_MSG(id, "", __VA_ARGS__)

extern "C" int cuda_get_devicecount(int* deviceCount)
{
	cudaError_t err;
	*deviceCount = 0;
	err = cudaGetDeviceCount(deviceCount);
	if (err != cudaSuccess)
	{
		if (err == cudaErrorNoDevice)
			printf("ERROR: NVIDIA no CUDA device found!\n");
		else if (err == cudaErrorInsufficientDriver)
			printf("WARNING: NVIDIA Insufficient driver!\n");
		else
			printf("WARNING: NVIDIA Unable to query number of CUDA devices!\n");
		return 0;
	}

	return 1;
}

extern "C" int cuda_get_deviceinfo(nvid_ctx* ctx)
{
	cudaError_t err;
	int version;

	err = cudaDriverGetVersion(&version);
	if (err != cudaSuccess)
	{
		printf("Unable to query CUDA driver version! Is an nVidia driver installed?\n");
		return 1;
	}

	if (version < CUDART_VERSION)
	{
		printf("Driver does not support CUDA %d.%d API! Update your nVidia driver!\n", CUDART_VERSION / 1000, (CUDART_VERSION % 1000) / 10);
		return 1;
	}

	int GPU_N;
	if (cuda_get_devicecount(&GPU_N) == 0)
	{
		return 1;
	}

	if (ctx->device_id >= GPU_N)
	{
		printf("Invalid device ID!\n");
		return 1;
	}

	cudaDeviceProp props;
	err = cudaGetDeviceProperties(&props, ctx->device_id);
	if (err != cudaSuccess)
	{
		printf("\nGPU %d: %s\n%s line %d\n", ctx->device_id, cudaGetErrorString(err), __FILE__, __LINE__);
		return 1;
	}

	ctx->device_name = strdup(props.name);
	ctx->device_mpcount = props.multiProcessorCount;
	ctx->device_arch[0] = props.major;
	ctx->device_arch[1] = props.minor;

	const int gpuArch = ctx->device_arch[0] * 10 + ctx->device_arch[1];

	//ctx->name = std::string(strdup(props.name));

	std::vector<int> arch;
#define XMRSTAK_PP_TOSTRING1(str) #str
#define XMRSTAK_PP_TOSTRING(str) XMRSTAK_PP_TOSTRING1(str)
	//char const * archStringList = XMRSTAK_PP_TOSTRING(XMRSTAK_CUDA_ARCH_LIST);
	char const * archStringList = "50"; // list generated in cmakelists file
#undef XMRSTAK_PP_TOSTRING
#undef XMRSTAK_PP_TOSTRING1
	std::stringstream ss(archStringList);

	//transform string list sperated with `+` into a vector of integers
	int tmpArch;
	while (ss >> tmpArch)
		arch.push_back(tmpArch);

	if (gpuArch >= 20 && gpuArch < 30)
	{
		// compiled binary must support sm_20 for fermi
		std::vector<int>::iterator it = std::find(arch.begin(), arch.end(), 20);
		if (it == arch.end())
		{
			printf("WARNING: NVIDIA GPU %d: miner not compiled for the gpu architecture %d.\n", ctx->device_id, gpuArch);
			return 5;
		}
	}
	if (gpuArch >= 30)
	{
		// search the minimum architecture greater than sm_20
		int minSupportedArch = 0;
		/* - for newer architecture than fermi we need at least sm_30
		* or a architecture >= gpuArch
		* - it is not possible to use a gpu with a architecture >= 30
		*   with a sm_20 only compiled binary
		*/
		for (int i = 0; i < arch.size(); ++i)
			if (minSupportedArch == 0 || (arch[i] >= 30 && arch[i] < minSupportedArch))
				minSupportedArch = arch[i];
		if (minSupportedArch < 30 || gpuArch < minSupportedArch)
		{
			printf("WARNING: NVIDIA GPU %d: miner not compiled for the gpu architecture %d.\n", ctx->device_id, gpuArch);
			return 5;
		}
	}

	// set all evice option those marked as auto (-1) to a valid value
	if (ctx->device_blocks == -1)
	{
		/* good values based of my experience
		*	 - 3 * SMX count >=sm_30
		*   - 2 * SMX count for <sm_30
		*/
		ctx->device_blocks = props.multiProcessorCount *
			(props.major < 3 ? 2 : 3);

		// increase bfactor for low end devices to avoid that the miner is killed by the OS
		if (props.multiProcessorCount <= 6)
			ctx->device_bfactor += 2;
	}
	if (ctx->device_threads == -1)
	{
		/* sm_20 devices can only run 512 threads per cuda block
		* `cryptonight_core_gpu_phase1` and `cryptonight_core_gpu_phase3` starts
		* `8 * ctx->device_threads` threads per block
		*/
		ctx->device_threads = 64;
		constexpr size_t byteToMiB = 1024u * 1024u;

		// no limit by default 1TiB
		size_t maxMemUsage = byteToMiB * byteToMiB;
		if (props.major == 6)
		{
			if (props.multiProcessorCount < 15)
			{
				// limit memory usage for GPUs for pascal < GTX1070
				maxMemUsage = size_t(2048u) * byteToMiB;
			}
			else if (props.multiProcessorCount <= 20)
			{
				// limit memory usage for GPUs for pascal GTX1070, GTX1080
				maxMemUsage = size_t(4096u) * byteToMiB;
			}
		}
		if (props.major < 6)
		{
			// limit memory usage for GPUs before pascal
			maxMemUsage = size_t(2048u) * byteToMiB;
		}
		if (props.major == 2)
		{
			// limit memory usage for sm 20 GPUs
			maxMemUsage = size_t(1024u) * byteToMiB;
		}

		if (props.multiProcessorCount <= 6)
		{
			// limit memory usage for low end devices to reduce the number of threads
			maxMemUsage = size_t(1024u) * byteToMiB;
		}

		int* tmp;
		cudaError_t err;
		// a device must be selected to get the right memory usage later on
		err = cudaSetDevice(ctx->device_id);
		if (err != cudaSuccess)
		{
			printf("WARNING: NVIDIA GPU %d: cannot be selected.\n", ctx->device_id);
			return 2;
		}
		// trigger that a context on the gpu will be allocated
		err = cudaMalloc(&tmp, 256);
		if (err != cudaSuccess)
		{
			printf("WARNING: NVIDIA GPU %d: context cannot be created.\n", ctx->device_id);
			return 3;
		}


		size_t freeMemory = 0;
		size_t totalMemory = 0;
		CUDA_CHECK(ctx->device_id, cudaMemGetInfo(&freeMemory, &totalMemory));

		CUDA_CHECK(ctx->device_id, cudaFree(tmp));
		// delete created context on the gpu
		CUDA_CHECK(ctx->device_id, cudaDeviceReset());

		ctx->total_device_memory = totalMemory;
		ctx->free_device_memory = freeMemory;

		size_t hashMemSize;
		hashMemSize = 2097152llu;

#ifdef WIN32
		/* We use in windows bfactor (split slow kernel into smaller parts) to avoid
		* that windows is killing long running kernel.
		* In the case there is already memory used on the gpu than we
		* assume that other application are running between the split kernel,
		* this can result into TLB memory flushes and can strongly reduce the performance
		* and the result can be that windows is killing the miner.
		* Be reducing maxMemUsage we try to avoid this effect.
		*/
		size_t usedMem = totalMemory - freeMemory;
		if (usedMem >= maxMemUsage)
		{
			printf("WARNING: NVIDIA GPU %d: already %s MiB memory in use, skip GPU.\n",
				ctx->device_id,
				std::to_string(usedMem / byteToMiB).c_str());
			return 4;
		}
		else
			maxMemUsage -= usedMem;

#endif
		// keep 128MiB memory free (value is randomly chosen)
		// 200byte are meta data memory (result nonce, ...)
		size_t availableMem = freeMemory - (128u * byteToMiB) - 200u;
		size_t limitedMemory = std::min(availableMem, maxMemUsage);
		// up to 16kibyte extra memory is used per thread for some kernel (lmem/local memory)
		// 680bytes are extra meta data memory per hash
		size_t perThread = hashMemSize + 16192u + 680u;
		size_t max_intensity = limitedMemory / perThread;
		ctx->device_threads = max_intensity / ctx->device_blocks;
		// use only odd number of threads
		ctx->device_threads = ctx->device_threads & 0xFFFFFFFE;

		if (props.major == 2 && ctx->device_threads > 64)
		{
			// Fermi gpus only support 512 threads per block (we need start 4 * configured threads)
			ctx->device_threads = 64;
		}

	}

	return 0;
}
