#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <cuda_runtime.h>
#include <assert.h>
#include <stdio.h>
#include "kernels.h"
using namespace std;

#include <iostream>
using std::cout;
using std::endl;


#define GPU_RUN(call,benchmark_name) {\
    const int mem_size = len*sizeof(int); \
    int* arr_in  = (int*)malloc(mem_size); \
    int* arr_out = (int*)malloc(mem_size); \
    for(int i=0; i<len; i++){ arr_in[i] = i+1; } \
    int* gpu_array_in; \
    int* gpu_array_out; \
    CUDASSERT(cudaMalloc((void **) &gpu_array_in, len*sizeof(int))); \
    CUDASSERT(cudaMalloc((void **) &gpu_array_out, len*sizeof(int))); \
    CUDASSERT(cudaMemcpy(gpu_array_in, arr_in, mem_size, cudaMemcpyHostToDevice));\
    CUDASSERT(cudaDeviceSynchronize());\
    cout << (benchmark_name) << endl; \
    gettimeofday(&t_startpar, NULL); \
    for(unsigned x = 0; x < RUNS; x++){ \
        (call); \
    }\
    CUDASSERT(cudaDeviceSynchronize());\
    gettimeofday(&t_endpar, NULL);\
    CUDASSERT(cudaMemcpy(arr_out, gpu_array_out, len*sizeof(int), cudaMemcpyDeviceToHost));\
    CUDASSERT(cudaDeviceSynchronize());\
    timeval_subtract(&t_diffpar, &t_endpar, &t_startpar);\
    unsigned long elapsed = t_diffpar.tv_sec*1e6+t_diffpar.tv_usec;\
    elapsed /= RUNS;\
    printf("    mean elapsed time was: %lu microseconds\n", elapsed);\
    printf("%d %d %d\n", arr_out[0], arr_out[10], arr_out[len-1]); \
    free(arr_in);\
    free(arr_out);\
    cudaFree(gpu_array_in);\
    cudaFree(gpu_array_out);\
}


static int timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1)
{
    unsigned int resolution=1000000;
    long int diff = (t2->tv_usec + resolution * t2->tv_sec) - (t1->tv_usec + resolution * t1->tv_sec);
    result->tv_sec = diff / resolution;
    result->tv_usec = diff % resolution;
    return (diff<0);
}


static inline void cudAssert(cudaError_t exit_code,
        const char *file,
        int         line) {
    if (exit_code != cudaSuccess) {
        fprintf(stderr, ">>> Cuda run-time error: %s, at %s:%d\n",
                cudaGetErrorString(exit_code), file, line);
        exit(exit_code);
    }
}
#define CUDASSERT(exit_code) { cudAssert((exit_code), __FILE__, __LINE__); }

template<int W>
void stencil_1d_tiled(
    const int * start,
    int * out,
    const unsigned len
    )
{
    const int block = 1024;
    const int grid = (len + (block-1)) / block;

    tiled_generic1d<W,block><<<grid,block>>>(start, out, len);
    CUDASSERT(cudaDeviceSynchronize());
}

template<int W>
void stencil_1d_global_read(
    const int * start,
    int * out,
    const unsigned len
    )
{
    const int block = 1024;
    const int grid = (len + (block-1)) / block;

    breathFirst_generic1d<W><<<grid,block>>>(start, out, len);
    CUDASSERT(cudaDeviceSynchronize());
}

#define call_kernel(kernel,blocksize) {\
    const int block = blocksize;\
    const int grid = (len + (block-1)) / block;\
    kernel;\
    CUDASSERT(cudaDeviceSynchronize());\
}

int main()
{
    struct timeval t_startpar, t_endpar, t_diffpar;
    const int W = 3;
    int RUNS = 100;
    {
        const int len = 1000000;

        GPU_RUN(call_kernel((breathFirst_generic1d<W><<<grid,block>>>(gpu_array_in, gpu_array_out, len)),1024),
                "## Benchmark GPU 1d global-mem ##");
        GPU_RUN(call_kernel((tiled_generic1d<W,block><<<grid,block>>>(gpu_array_in, gpu_array_out, len)),1024),
                "## Benchmark GPU 1d tiled ##"); // best for very large W
        GPU_RUN(call_kernel((inlinedIndexesBreathFirst_generic1d<W><<<grid,block>>>(gpu_array_in, gpu_array_out, len)),1024),
                "## Benchmark GPU 1d inlined global read indxs ##");
        GPU_RUN(call_kernel((outOfSharedtiled_generic1d<W,block><<<grid,block>>>(gpu_array_in, gpu_array_out, len)),1024),
                "## Benchmark GPU 1d out of shared tiled ##"); //best for somewhat large W, but not very large
        GPU_RUN(call_kernel((inSharedtiled_generic1d<W,block><<<grid,block>>>(gpu_array_in, gpu_array_out, len)),(1024-2*W)),
                "## Benchmark GPU 1d in shared tiled ##"); //best for somewhat large W, but not very large
    }

    return 0;
}

















/*static void sevenPointStencil(
        float * start,
        float * swap_out,
        const unsigned nx,
        const unsigned ny,
        const unsigned nz,
        const unsigned iterations // must be odd
        )
{
    const int T = 32;
    const int dimx = (nz + (T-1))/T;
    const int dimy = (ny + (T-1))/T;
    dim3 block(T,T,1);
    dim3 grid(dimx, dimy, 1);

    for (unsigned i = 0; i < iterations; ++i){
        if(i & 1){
            sevenPointStencil_single_iter<<< grid,block >>>(swap_out, start, nx, ny, nz);
        }
        else {
            sevenPointStencil_single_iter<<< grid,block >>>(start, swap_out, nx, ny, nz);
        }
    }
    CUDASSERT(cudaDeviceSynchronize());

}

static void sevenPointStencil_tiledSliding(
        float * start,
        float * swap_out,
        const unsigned nx,
        const unsigned ny,
        const unsigned nz,
        const unsigned iterations // must be odd
        )
{
    const int T = 32;
    const int dimx = (nz + (T-1))/T;
    const int dimy = (ny + (T-1))/T;
    dim3 block(T,T,1);
    dim3 grid(dimx, dimy, 1);

    for (unsigned i = 0; i < iterations; ++i){
        if(i & 1){
            sevenPointStencil_single_iter_tiled_sliding <<< grid,block >>>(swap_out, start, nx, ny, nz);
        }
        else {
            sevenPointStencil_single_iter_tiled_sliding <<< grid,block >>>(start, swap_out, nx, ny, nz);
        }
    }
    CUDASSERT(cudaDeviceSynchronize());

}
static void sevenPointStencil_tiledSliding_fully(
        float * start,
        float * swap_out,
        const unsigned nx,
        const unsigned ny,
        const unsigned nz,
        const unsigned iterations // must be odd
        )
{
    const unsigned T = 32;
    const unsigned Ts = 6;
    const unsigned dimx = (nx + (T-1))/T;
    const unsigned dimy = (ny + (Ts-1))/Ts;
    const unsigned dimz = (nz + (Ts-1))/Ts;
    dim3 block(32,6,6);
    dim3 grid(dimx, dimy, dimz);

    for (unsigned i = 0; i < iterations; ++i){
        if(i & 1){
            sevenPointStencil_single_iter_tiled_sliding_read<<<grid,block>>>(swap_out, start, nx, ny, nz);
        }
        else {
            sevenPointStencil_single_iter_tiled_sliding_read<<<grid,block>>>(start, swap_out, nx, ny, nz);
        }
    }
    CUDASSERT(cudaDeviceSynchronize());

}*/