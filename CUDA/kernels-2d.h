#ifndef KERNELS2D
#define KERNELS2D

#include <cuda_runtime.h>
#include "constants.h"

/*******************************************************************************
 * Helper functions
 */

template<
    const int amin_x, const int amin_y,
    const int sh_size_x,  const int sh_size_flat,
    const int group_size_x,  const int group_size_y>
__device__
__forceinline__
void bigtile_flat_loader_addcarry_new(
    const T* A,
    T tile[sh_size_flat],
    const long len_x, const long len_y,
    const int loc_flat,
    const long block_offset_x, const long block_offset_y)
{
    constexpr int blockDimFlat = group_size_x * group_size_y;
    constexpr int sh_span_y = sh_size_x;
    constexpr int iters = CEIL_DIV(sh_size_flat, blockDimFlat);

    const long max_ix_x = len_x - 1;
    const long max_ix_y = len_y - 1;

    constexpr int loader_cap_x = sh_size_x + amin_x;

    int local_flat = loc_flat;
    int local_y = (loc_flat / sh_span_y) + amin_y;
    int local_x = (loc_flat % sh_span_y) + amin_x;

    constexpr int add_y = blockDimFlat / sh_span_y;
    constexpr int add_x = blockDimFlat % sh_span_y;

    for(int i = 0; i < iters; i++){
        const long gx = BOUNDL((local_x + block_offset_x), max_ix_x);
        const long gy = BOUNDL((local_y + block_offset_y), max_ix_y);

        const long index = gy * len_x + gx;
        if(local_flat < sh_size_flat){
            tile[local_flat] = A[index];
        }

        // add
        local_flat += blockDimFlat;
        local_x += add_x;
        local_y += add_y;

        // carry
        if(local_x >= loader_cap_x){
            local_x -= sh_size_x;
            local_y += 1;
        }
    }
}


template<
    const int amin_x, const int amin_y,
    const int amax_x, const int amax_y
    , const int sh_size_x,  const int sh_size_flat
    >
__device__
__forceinline__
void write_from_shared_flat(
    const T tile[],
    T* __restrict__ const out,
    const long2 lens,
    const int2 locals,
    const long2 block_offsets)
{
    constexpr int2 range = {
        amax_x - amin_x + 1,
        amax_y - amin_y + 1};
    constexpr int total_range = range.x * range.y;

    const long gid_x = block_offsets.x + long(locals.x);
    const long gid_y = block_offsets.y + long(locals.y);

    T vals[total_range];

    const bool should_write = ((gid_x < lens.x) && (gid_y < lens.y));
    if(should_write){
        const long gid_flat = gid_y * lens.x + gid_x;

        for(int j=0; j < range.y; j++){
            const int y = locals.y + j;
            for(int k=0; k < range.x; k++){
                const int idx = j*range.x + k;
                const int x = locals.x + k;
                const int index = y*sh_size_x + x;
                vals[idx] = tile[long(index)];
            }
        }

        T sum_acc = 0;
        for(int j=0; j < range.y; j++){
            for(int k=0; k < range.x; k++){
                const int idx = j*range.x + k;
                sum_acc += vals[idx];
            }
        }
        sum_acc /= (T)total_range;
        out[gid_flat] = sum_acc;
    }
}

template<
    const int amin_x, const int amin_y,
    const int amax_x, const int amax_y
    , const int sh_size_x,  const int sh_size_y
    >
__device__
__forceinline__
void write_from_shared_cube(
    const T tile2d[sh_size_y][sh_size_x],
    T* __restrict__ const out,
    const long2 lens,
    const int2 locals,
    const long2 block_offsets)
{
    constexpr int2 range = {
        amax_x - amin_x + 1,
        amax_y - amin_y + 1};
    constexpr int total_range = range.x * range.y;

    const long gid_x = block_offsets.x + long(locals.x);
    const long gid_y = block_offsets.y + long(locals.y);

    T vals[total_range];

    const bool should_write = ((gid_x < lens.x) && (gid_y < lens.y));
    if(should_write){
        const long gid_flat = gid_y * lens.x + gid_x;

        for(int j=0; j < range.y; j++){
            for(int k=0; k < range.x; k++){
                const int idx = j*range.x + k;
                const int y = locals.y + j;
                const int x = locals.x + k;
                vals[idx] = tile2d[y][x];
            }
        }

        T sum_acc = 0;
        for(int j=0; j < range.y; j++){
            for(int k=0; k < range.x; k++){
                const int idx = j*range.x + k;
                sum_acc += vals[idx];
            }
        }
        sum_acc /= (T)total_range;
        out[gid_flat] = sum_acc;
    }
}

template<
    const int amin_x, const int amin_y,
    const int sh_size_x, const int sh_size_flat,
    const int group_size_x,  const int group_size_y>
__device__
__forceinline__
void bigtile_flat_loader_divrem(
    const T* __restrict__ A,
    T tile[],
    const long2 lens,
    const int local_flat,
    const long2 block_offsets)
{
    constexpr int blockDimFlat = group_size_x * group_size_y;
    constexpr int iters = CEIL_DIV(sh_size_flat, blockDimFlat);

    const long max_ix_x = lens.x - 1;
    const long max_ix_y = lens.y - 1;

    const long view_offset_x = block_offsets.x + long(amin_x);
    const long view_offset_y = block_offsets.y + long(amin_y);

    for(int i = 0; i < iters; i++){
        const int local_ix = (i * blockDimFlat) + local_flat;

        // div/rem
        const int local_y = local_ix / sh_size_x;
        const int local_x = local_ix % sh_size_x;

        const long gy = BOUNDL((long(local_y) + view_offset_y), max_ix_y);
        const long gx = BOUNDL((long(local_x) + view_offset_x), max_ix_x);

        const long index = gy * lens.x + gx;
        if(local_ix < sh_size_flat){
            tile[long(local_ix)] = A[index];
        }
    }
}



template<
    const int amin_x, const int amin_y,
    const int sh_size_x, const int sh_size_y,
    const int group_size_x,  const int group_size_y>
__device__
__forceinline__
void bigtile_cube_loader(
    const T* __restrict__ A,
    T tile2d[sh_size_y][sh_size_x],
    const long2 lens,
    const int2 locals,
    const long2 block_offsets)
{
    const long max_x_ix = lens.x - 1;
    const long max_y_ix = lens.y - 1;

    const int x_iters = divUp(sh_size_x,group_size_x);
    const int y_iters = divUp(sh_size_y,group_size_y);

    for(int i = 0; i < y_iters; i++){
        const int local_y = locals.y + i*group_size_y;
        const long gy = bound( long(local_y) + block_offsets.y + long(amin_y), max_y_ix)
                     * lens.x;

        for(int j = 0; j < x_iters; j++){
            const int local_x = locals.x + j*group_size_x;
            const long gx = bound( long(local_x) + block_offsets.x + long(amin_x), max_x_ix);
            if(local_x < sh_size_x && local_y < sh_size_y){
                tile2d[local_y][local_x] = A[gx + gy];
            }
        }
    }
}

template<
    const int amin_x, const int amin_y,
    const int sh_size_x,  const int sh_size_flat,
    const int group_size_x,  const int group_size_y>
__device__
__forceinline__
void bigtile_flat_loader_addcarry(
    const T* __restrict__ A,
    T tile[],
    const long2 lens,
    const int loc_flat,
    const long2 block_offsets)
{
    constexpr int blockDimFlat = group_size_x * group_size_y;
    constexpr int iters = CEIL_DIV(sh_size_flat, blockDimFlat);

    const long max_ix_x = lens.x - 1;
    const long max_ix_y = lens.y - 1;

    const long view_offset_x = block_offsets.x + long(amin_x);
    const long view_offset_y = block_offsets.y + long(amin_y);

    int local_flat = loc_flat;
    int local_y = loc_flat / sh_size_x;
    int local_x = loc_flat % sh_size_x;

    const int add_y = blockDimFlat / sh_size_x;
    const int add_x = blockDimFlat % sh_size_x;

    for(int i = 0; i < iters; i++){
        const long gy = bound((long(local_y) + view_offset_y), max_ix_y);
        const long gx = bound((long(local_x) + view_offset_x), max_ix_x);

        const long index = gy * lens.x + gx;
        if(local_flat < sh_size_flat){
            tile[local_flat] = A[index];
        }

        // add
        local_flat += blockDimFlat;
        local_x += add_x;
        local_y += add_y;

        // carry
        if(local_x >= sh_size_x){
            local_x -= sh_size_x;
            local_y += 1;
        }
    }
}

/*******************************************************************************
 * Versions where the indices are inlined and we are provided a
 * associative and commutative operator with a neutral element
 * and then a map
 * (here summation followed by division, aka taking the average).
 */
template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x,  const int group_size_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void global_reads_2d_inline_multiDim(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long2 lens
    )
{
    constexpr int2 range = {
        amax_x - amin_x + 1,
        amax_y - amin_y + 1};
    constexpr int total_range = range.y * range.x;

    const long gidx = blockIdx.x*group_size_x + threadIdx.x;
    const long gidy = blockIdx.y*group_size_y + threadIdx.y;

    if (gidx < lens.x && gidy < lens.y)
    {
        const long gid_flat = gidy * lens.x + gidx;
        const long max_ix_x = lens.x - 1;
        const long max_ix_y = lens.y - 1;

        T sum_acc = 0;
        for(int j=amin_y; j <= amax_y; j++){
            const long y = BOUNDL(gidy + j, max_ix_y);
            for(int k=amin_x; k <= amax_x; k++){
                const long x = BOUNDL(gidx + k, max_ix_x);
                const long index = y*lens.x + x;
                sum_acc += A[index];
            }
        }
        sum_acc /= (T)total_range;
        out[gid_flat] = sum_acc;
    }
}


template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x,  const int group_size_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void global_reads_2d_inline_singleDim(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long2 lens,
    const int2 grid
    )
{
    constexpr int2 range = {
        amax_x - amin_x + 1,
        amax_y - amin_y + 1};
    constexpr int total_range = range.y * range.x;

    const int group_id_flat = blockIdx.x;
    const long group_id_y = group_id_flat / grid.x;
    const long group_id_x = group_id_flat % grid.x;

    const int local_id_flat = threadIdx.x;
    const int local_id_y = local_id_flat / group_size_x;
    const int local_id_x = local_id_flat % group_size_x;

    const long gidx = group_id_x*group_size_x + local_id_x;
    const long gidy = group_id_y*group_size_y + local_id_y;

    if (gidx < lens.x && gidy < lens.y)
    {
        const long gid_flat = gidy * lens.x + gidx;
        const long max_ix_x = lens.x - 1;
        const long max_ix_y = lens.y - 1;

        T sum_acc = 0;
        for(int j=amin_y; j <= amax_y; j++){
            const long y = BOUNDL(gidy + j, max_ix_y);
            for(int k=amin_x; k <= amax_x; k++){
                const long x = BOUNDL(gidx + k, max_ix_x);
                const long index = y*lens.x + x;
                sum_acc += A[index];
            }
        }
        sum_acc /= (T)total_range;
        out[gid_flat] = sum_acc;
    }
}

template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x,  const int group_size_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void big_tile_2d_inlined_flat_divrem_singleDim(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long2 lens,
    const int2 grid
    )
{
    constexpr int sh_size_x =  group_size_x + (amax_x - amin_x);
    constexpr int sh_size_y =  group_size_y + (amax_y - amin_y);
    constexpr int sh_size_flat = sh_size_x * sh_size_y;
    extern __shared__ T tile[];

    const int loc_flat = threadIdx.x;
    const int loc_y = loc_flat / group_size_x;
    const int loc_x = loc_flat % group_size_x;
    const int2 locals = { loc_x, loc_y };

    int group_id_y = blockIdx.x / grid.x;
    int group_id_x = blockIdx.x % grid.x;

    const long2 writeSet_offset = {
        long(group_id_x) * long(group_size_x),
        long(group_id_y) * long(group_size_y)};

    bigtile_flat_loader_divrem
        <amin_x,amin_y
        ,sh_size_x,sh_size_flat
        ,group_size_x,group_size_y>
        (A, tile, lens, loc_flat, writeSet_offset);

    __syncthreads();

    write_from_shared_flat
        <amin_x,amin_y
        ,amax_x,amax_y
        ,sh_size_x,sh_size_flat>
        (tile, out, lens, locals, writeSet_offset);

}

template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x,  const int group_size_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void big_tile_2d_inlined_cube_singleDim(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long2 lens,
    const int2 grid
    )
{
    const int waste_x = amax_x - amin_x;
    const int waste_y = amax_y - amin_y;
    const int sh_size_x = group_size_x + waste_x;
    const int sh_size_y = group_size_y + waste_y;

    __shared__ T tile2d[sh_size_y][sh_size_x];

    const int loc_flat = threadIdx.x;
    const int loc_y = loc_flat / group_size_x;
    const int loc_x = loc_flat % group_size_x;
    const int2 locals = { loc_x, loc_y };

    int group_id_y = blockIdx.x / grid.x;
    int group_id_x = blockIdx.x % grid.x;

    const long2 writeSet_offset = {
        long(group_id_x) * long(group_size_x),
        long(group_id_y) * long(group_size_y)};

    bigtile_cube_loader
        <amin_x,amin_y
        ,sh_size_x,sh_size_y
        ,group_size_x,group_size_y>
        (A, tile2d, lens, locals, writeSet_offset);

    __syncthreads();


    write_from_shared_cube
        <amin_x,amin_y
        ,amax_x,amax_y
        ,sh_size_x,sh_size_y>
        (tile2d, out, lens, locals, writeSet_offset);

}


template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x,  const int group_size_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void big_tile_2d_inlined_flat_addcarry_singleDim(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long2 lens,
    const int2 grid
    )
{
    constexpr int sh_size_x = group_size_x + (amax_x - amin_x);
    constexpr int sh_size_y = group_size_y + (amax_y - amin_y);
    constexpr int sh_size_flat = sh_size_x * sh_size_y;
    extern __shared__ T tile[];

    const int loc_flat = threadIdx.x;
    const int loc_y = loc_flat / group_size_x;
    const int loc_x = loc_flat % group_size_x;
    const int2 locals = { loc_x, loc_y };

    int group_id_y = blockIdx.x / grid.x;
    int group_id_x = blockIdx.x % grid.x;

    const long2 writeSet_offset = {
        long(group_id_x) * long(group_size_x),
        long(group_id_y) * long(group_size_y)};

    bigtile_flat_loader_addcarry
        <amin_x,amin_y
        ,sh_size_x,sh_size_flat
        ,group_size_x,group_size_y>
        (A, tile, lens, loc_flat, writeSet_offset);

    __syncthreads();

    write_from_shared_flat
        <amin_x,amin_y
        ,amax_x,amax_y
        ,sh_size_x,sh_size_flat>
        (tile, out, lens, locals, writeSet_offset);

}

template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x,  const int group_size_y
, const int strip_x, const int strip_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void stripmine_big_tile_2d_inlined_flat_addcarry_singleDim(
    const T* A,
    T* out,
    const long2 lens,
    const int2 strip_grid
    )
{
    extern __shared__ T tile[];
    constexpr int sh_size_x = strip_x*group_size_x + (amax_x - amin_x);
    constexpr int sh_size_y = strip_y*group_size_y + (amax_y - amin_y);
    constexpr int sh_size_flat = sh_size_x * sh_size_y;

    constexpr long strip_id_scaler_x = strip_x * group_size_x;
    constexpr long strip_id_scaler_y = strip_y * group_size_y;

    const int strip_grid_spans_y = strip_grid.x;

    const long strip_id_flat = blockIdx.x;
    const long strip_id_y = strip_id_flat / strip_grid_spans_y;
    const long strip_id_x = strip_id_flat % strip_grid_spans_y;

    constexpr int grp_span_y = group_size_x;
    const int loc_flat = threadIdx.x;
    const int loc_y = loc_flat / grp_span_y;
    const int loc_x = loc_flat % grp_span_y;

    const long block_offset_x = strip_id_x * strip_id_scaler_x;
    const long block_offset_y = strip_id_y * strip_id_scaler_y;

    const long2 block_offsets = {block_offset_x,block_offset_y};

    bigtile_flat_loader_addcarry_new
        <amin_x,amin_y
        ,sh_size_x,sh_size_flat
        ,group_size_x,group_size_y>
        (A, tile
         , lens.x, lens.y
         , loc_flat
         , block_offset_x, block_offset_y);

    // the tile has to be fully done being loaded before we start reading
    __syncthreads();
    for(int j__ = 0; j__ < strip_y; j__++){
        for(int k__ = 0; k__ < strip_x; k__++){
            // tile_offsets implicitly also handle the change in group_id
            const int tile_offset_x = loc_x + (k__ * group_size_x);
            const int tile_offset_y = loc_y + (j__ * group_size_y);

            const int2 tile_offsets = {tile_offset_x, tile_offset_y};

            write_from_shared_flat
                <amin_x,amin_y
                ,amax_x,amax_y
                ,sh_size_x,sh_size_flat>
                (tile, out,
                 lens,
                 tile_offsets,
                 block_offsets);
        }
    }
}

template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_flat
, const int windows_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void sliding_tile_flat_smalltile_singleDim(
    const T* A,
    T* out,
    const long2 lens,
    const int2 strip_grid
    )
{
    extern __shared__ T tile[];
    constexpr int sh_size_x = group_size_flat;
    constexpr int range_exc_x = amax_x - amin_x;
    constexpr int range_exc_y = amax_y - amin_y;
    constexpr int range_inc_y = range_exc_y + 1;

    constexpr int sh_size_y = range_inc_y;
    constexpr int sh_size_flat = sh_size_y * sh_size_x;

    constexpr int working_x = sh_size_x - range_exc_x;
    constexpr int2 range = {
        amax_x - amin_x + 1,
        amax_y - amin_y + 1};
    constexpr int total_range = range.x * range.y;
    constexpr int tile_start_y = (sh_size_y + (amin_y % sh_size_y));

    const int strip_grid_spans_y = strip_grid.x;

    const int strip_id_flat = blockIdx.x;
    const int strip_id_y = strip_id_flat / strip_grid_spans_y;
    const int strip_id_x = strip_id_flat % strip_grid_spans_y;
    const long strip_offset_y = strip_id_y * windows_y;
    const long strip_offset_x = strip_id_x * working_x;

    const int read_write_tile_x = threadIdx.x;
    const long write_gid_x = read_write_tile_x + strip_offset_x;
    const long read_gid_x = bound(write_gid_x + amin_x, lens.x-1);
    const long max_y = lens.y - 1;

    constexpr int tile_y_start = (tile_start_y * sh_size_x)%sh_size_flat;
    int write_tile_y = tile_y_start+read_write_tile_x;
    long write_gid_y = strip_offset_y + amin_y;
    for(int i__ = 0; i__ < range_exc_y; i__++){
        if(write_gid_y < 0){
            tile[write_tile_y] = A[0*lens.x + read_gid_x];
        }
        else if(write_gid_y > max_y){
            tile[write_tile_y] = A[max_y*lens.x + read_gid_x];
        }
        else{
            tile[write_tile_y] = A[write_gid_y*lens.x + read_gid_x];
        }
        write_tile_y += sh_size_x;
        if(write_tile_y >= sh_size_flat){ write_tile_y -= sh_size_flat; }
        write_gid_y++;
    }
    long gidy = strip_offset_y;
    int read_tile_y = tile_y_start;
    const bool should_write_x = write_gid_x < lens.x && read_write_tile_x < working_x;
    const int iters = int(min(long(windows_y),max_y+1-strip_offset_y));
    for(int y__ = 0; y__ < iters; y__++){
        __syncthreads(); // cross iteration depedency on tile
        if(write_gid_y < 0){
            tile[write_tile_y] = A[0*lens.x + read_gid_x];
        }
        else if(write_gid_y > max_y){
            tile[write_tile_y] = A[max_y*lens.x + read_gid_x];
        }
        else{
            tile[write_tile_y] = A[write_gid_y*lens.x + read_gid_x];
        }
        __syncthreads(); // finish write to tile before we read
        if(should_write_x){
            T vals[total_range];
            for(int j=0; j < range.y; j++){
                const int y = j*sh_size_x;
                const int ty = y + read_tile_y;
                const int tty = ty - (ty >= sh_size_flat ? sh_size_flat : 0);
                for(int k=0; k < range.x; k++){
                    const int idx = j*range.x + k;
                    vals[idx] = tile[read_write_tile_x + tty + k];
                }
            }

            T sum_acc = 0;
            for(int j=0; j < range.y; j++){
                for(int k=0; k < range.x; k++){
                    const int idx = j*range.x + k;
                    sum_acc += vals[idx];
                }
            }
            sum_acc /= (T)total_range;
            out[gidy * lens.x + write_gid_x] = sum_acc;

        }
        gidy++;
        write_tile_y += sh_size_x;
        if(write_tile_y >= sh_size_flat){ write_tile_y -= sh_size_flat; }
        write_gid_y++;
        read_tile_y += sh_size_x;
        if(read_tile_y >= sh_size_flat){ read_tile_y -= sh_size_flat; }
    }
}

template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x, const int group_size_y
, const int windows_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void sliding_tile_smalltile_singleDim(
    const T* A,
    T* out,
    const long2 lens,
    const int2 strip_grid
    )
{
    extern __shared__ T tile[];
    constexpr int range_exc_x = amax_x - amin_x;
    constexpr int range_exc_y = amax_y - amin_y;
    constexpr int2 range = {
        range_exc_x + 1,
        range_exc_y + 1};
    constexpr int total_range = range.x * range.y;
    constexpr int group_size_flat = group_size_x * group_size_y;
    constexpr int sh_size_x = group_size_x;

    constexpr int sh_used_spac_y = range.y + group_size_y;

    // magic to get next power of 2
    // source: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
    constexpr int r0 = sh_used_spac_y-1;
    constexpr int r1 = r0 | (r0 >> 1);
    constexpr int r2 = r1 | (r1 >> 2);
    constexpr int r3 = r2 | (r2 >> 4);
    constexpr int r4 = r3 | (r3 >> 8);
    constexpr int r5 = r4 | (r4 >> 16);
    constexpr int r6 = r5+1;
    //
    constexpr int sh_size_y = r6;
    constexpr int sh_size_flat = sh_size_y*sh_size_x;
    constexpr int working_x = sh_size_x - range_exc_x;
    constexpr int tile_off_y = (sh_size_y + (amin_y % sh_size_y));

    const int strip_grid_spans_y = strip_grid.x;

    const int local_flat = threadIdx.x;
    const int local_y = local_flat / group_size_x;
    const int local_x = local_flat % group_size_x;

    const int strip_id_flat = blockIdx.x;
    const int strip_id_y = strip_id_flat / strip_grid_spans_y;
    const int strip_id_x = strip_id_flat % strip_grid_spans_y;
    const long strip_offset_y = strip_id_y * (group_size_y *  windows_y);
    const long strip_offset_x = strip_id_x * working_x;

    const long gidx = local_x + strip_offset_x;
    const long lgidx = bound(gidx + amin_x, lens.x-1);
    const long lst_gid = (lens.y-1)*lens.x+lgidx;
    const bool should_write_x = gidx < lens.x && local_x < working_x;

    const long flat_length = lens.x * lens.y;
    long igid = (strip_offset_y + amin_y + local_y)*lens.x+lgidx;
    int tile_flat = ((local_y + tile_off_y) % sh_size_y)*sh_size_x+local_x;
    constexpr int last_cap = sh_used_spac_y % group_size_y;
    constexpr int setup_iters = divUp(sh_used_spac_y,group_size_y);
    for(int y__ = 0; y__ < setup_iters; y__++){
        long gix = igid;
        if(igid < 0){
            gix = lgidx;
        }
        if(igid >= flat_length){
            gix = lst_gid;
        }
        if(y__ < (setup_iters-1) ||  (local_y < last_cap)){
            tile[tile_flat] = A[gix];
        }
        igid += group_size_y*lens.x;
        tile_flat += group_size_flat;
        if(tile_flat >= sh_size_flat){ tile_flat -= sh_size_flat; }
    }

    constexpr int tile_loader_y = tile_off_y + range_exc_y;
    tile_flat = ((local_y + tile_loader_y) % sh_size_y)*sh_size_x+local_x;
    igid = (strip_offset_y + amax_y + local_y)*lens.x+lgidx;
    long gid_flat = (strip_offset_y + local_y)*lens.x+gidx;

    for(int y__ = 0; y__ < windows_y; y__++){
        __syncthreads(); // cross iteration depedency on tile
        const int y = y__ * group_size_y + local_y;
        {
            long gix = igid;
            if(igid < 0){
                gix = lgidx;
            }
            if(igid >= flat_length){
                gix = lst_gid;
            }
            tile[tile_flat] = A[gix];
        }
        __syncthreads();
        {
            T vals[total_range];
            const bool should_write = should_write_x && gid_flat < flat_length;
            if(should_write){
                for(int j=0; j < range.y; j++){
                    for(int k=0; k < range.x; k++){
                        const int ty = (tile_off_y+j+y) % sh_size_y;
                        const int tx = local_x+k;
                        const int idx = j*range.x + k;
                        vals[idx] = tile[ty*sh_size_x+tx];
                    }
                }

                T sum_acc = 0;
                for(int j=0; j < range.y; j++){
                    for(int k=0; k < range.x; k++){
                        const int idx = j*range.x + k;
                        sum_acc += vals[idx];
                    }
                }
                sum_acc /= (T)total_range;
                out[gid_flat] = sum_acc;
            }
        }
        igid += group_size_y*lens.x;
        gid_flat += group_size_y*lens.x;
        tile_flat += group_size_flat;
        if(tile_flat >= sh_size_flat){ tile_flat -= sh_size_flat; }
    }
}

template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x,  const int group_size_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void virtual_addcarry_big_tile_2d_inlined_flat_addcarry_singleDim(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long2 lens,
    const int num_phys_groups,
    const int2 virtual_grid
    )
{
    constexpr int sh_size_x = group_size_x + (amax_x - amin_x);
    constexpr int sh_size_y = group_size_y + (amax_y - amin_y);
    constexpr int sh_size_flat = sh_size_x * sh_size_y;
    extern __shared__ T tile[];

    const int virtual_grid_flat = virtual_grid.x * virtual_grid.y;
    const int iters_per_phys = CEIL_DIV(virtual_grid_flat, num_phys_groups);
    const int id_add_y = num_phys_groups / virtual_grid.x;
    const int id_add_x = num_phys_groups % virtual_grid.x;

    const int loc_flat = threadIdx.x;
    const int loc_y = loc_flat / group_size_x;
    const int loc_x = loc_flat % group_size_x;
    const int2 locals = { loc_x, loc_y };

    int group_id_y = blockIdx.x / virtual_grid.x;
    int group_id_x = blockIdx.x % virtual_grid.x;

    int virtual_group_id_flat = blockIdx.x;
    for(int i=0; i<iters_per_phys;i++){
        const long2 writeSet_offset = {
                long(group_id_x) * long(group_size_x),
                long(group_id_y) * long(group_size_y)};

        bigtile_flat_loader_addcarry
            <amin_x,amin_y
            ,sh_size_x,sh_size_flat
            ,group_size_x,group_size_y>
            (A, tile, lens, loc_flat, writeSet_offset);

        __syncthreads();

        write_from_shared_flat
            <amin_x,amin_y
            ,amax_x,amax_y
            ,sh_size_x,sh_size_flat>
            (tile, out, lens, locals, writeSet_offset);

        // add
        virtual_group_id_flat += num_phys_groups;
        group_id_x += id_add_x;
        group_id_y += id_add_y;

        // carry
        if(group_id_x >= virtual_grid.x){
            group_id_x -= virtual_grid.x;
            group_id_y += 1;
        }
        // need to sync so there are no inter-iteration tile issues
        __syncthreads();
    }
}

template
< const int amin_x, const int amin_y
, const int amax_x, const int amax_y
, const int group_size_x,  const int group_size_y
, const int strip_x, const int strip_y
>
__global__
__launch_bounds__(BLOCKSIZE)
void virtual_addcarry_stripmine_big_tile_2d_inlined_flat_addcarry_singleDim(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long2 lens,
    const int num_phys_groups,
    const int2 virtual_grid
    )
{
    constexpr int2 strips = { strip_x, strip_y };
    constexpr int sh_size_x = strips.x*group_size_x + (amax_x - amin_x);
    constexpr int sh_size_y = strips.y*group_size_y + (amax_y - amin_y);
    constexpr int sh_size_flat = sh_size_x * sh_size_y;
    extern __shared__ T tile[];

    constexpr long strip_size_x = group_size_x*strip_x;
    constexpr long strip_size_y = group_size_y*strip_y;
    const long2 virtual_grid_strip = {
        divUp(lens.x, strip_size_x),
        divUp(lens.y, strip_size_y) };

    const int virtual_grid_strip_flat = virtual_grid_strip.x * virtual_grid_strip.y;
    const int iters_per_phys = divUp(virtual_grid_strip_flat, num_phys_groups);
    const int strip_id_add_y = (num_phys_groups / virtual_grid_strip.x);
    const int strip_id_add_x = (num_phys_groups % virtual_grid_strip.x);

    const int loc_flat = threadIdx.x;
    const int loc_y = loc_flat / group_size_x;
    const int loc_x = loc_flat % group_size_x;

    const int start_group_id_flat = blockIdx.x;
    int strip_id_y   = start_group_id_flat / virtual_grid_strip.x;
    int strip_id_x   = start_group_id_flat % virtual_grid_strip.x;

    int strip_id_flat = start_group_id_flat;

    for(int i=0; i<iters_per_phys;i++){
        const int base_group_id_x = strip_id_x * strips.x;
        const int base_group_id_y = strip_id_y * strips.y;

        const long2 base_block_offset = {
            long(base_group_id_x) * long(group_size_x),
            long(base_group_id_y) * long(group_size_y)};

        bigtile_flat_loader_addcarry
            <amin_x,amin_y
            ,sh_size_x,sh_size_flat
            ,group_size_x,group_size_y>
            (A, tile, lens, loc_flat, base_block_offset
             );

        // the tile has to be fully done being loaded before we start reading
        __syncthreads();
        for(int j__ = 0; j__ < strips.y; j__++){
            const int tile_offset_y = loc_y + (j__ * group_size_y);
            for(int k__ = 0; k__ < strips.x; k__++){
                // tile_offsets implicitly also handle the change in group_id
                const int tile_offset_x = loc_x + (k__ * group_size_x);
                const int2 locals = { tile_offset_x, tile_offset_y };

                write_from_shared_flat
                    <amin_x,amin_y
                    ,amax_x,amax_y
                    ,sh_size_x,sh_size_flat
                    >
                    (tile, out, lens, locals, base_block_offset);
            }
        }
        // add
        strip_id_flat += num_phys_groups;
        strip_id_x += strip_id_add_x;
        strip_id_y += strip_id_add_y;

        // carry
        if(strip_id_x >= virtual_grid_strip.x){
            strip_id_x -= virtual_grid_strip.x;
            strip_id_y += 1;
        }

        // need to sync so there are no inter-iteration tile issues
        __syncthreads();
    }
}


/*
template<long ixs_len>
__global__
void global_reads_2d(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long row_len,
    const long col_len
    )
{
    const long gidx = blockIdx.x*SQ_BLOCKSIZE + threadIdx.x;
    const long gidy = blockIdx.y*SQ_BLOCKSIZE + threadIdx.y;
    const long gindex = gidy * row_len + gidx;
    const long max_x_ix = row_len - 1;
    const long max_y_ix = col_len - 1;

    if (gidx < row_len && gidy < col_len)
    {
        T sum_acc = 0;

        for (long i = 0; i < ixs_len; i++ ){
            const long y = BOUNDL(gidy + ixs_2d[i].y, max_y_ix);
            const long x = BOUNDL(gidx + ixs_2d[i].x, max_x_ix);
            const long index = y * row_len + x;
            sum_acc += A[index];
        }
        out[gindex] = sum_acc / ixs_len;
    }
}


template<long ixs_len, long x_axis_min, long x_axis_max, long y_axis_min, long y_axis_max>
__global__
void small_tile_2d(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long row_len,
    const long col_len
    )
{
    __shared__ T tile[SQ_BLOCKSIZE][SQ_BLOCKSIZE];
    const long waste_x = x_axis_min + x_axis_max;
    const long waste_y = y_axis_min + y_axis_max;
    const long gidx = blockIdx.x*(SQ_BLOCKSIZE - waste_x) + threadIdx.x - x_axis_min;
    const long gidy = blockIdx.y*(SQ_BLOCKSIZE - waste_y) + threadIdx.y - y_axis_min;
    const long gindex = gidy * row_len + gidx;
    const long max_x_ix = row_len - 1;
    const long max_y_ix = col_len - 1;
    const long x = BOUNDL(gidx, max_x_ix);
    const long y = BOUNDL(gidy, max_y_ix);
    const long index = y * row_len + x;
    tile[threadIdx.y][threadIdx.x] = A[index];
    __syncthreads();

    if (    (0 <= gidx && gidx < row_len)
        &&  (x_axis_min <= threadIdx.x && threadIdx.x < SQ_BLOCKSIZE - x_axis_max)
        &&  (0 <= gidy && gidy < col_len)
        &&  (y_axis_min <= threadIdx.y && threadIdx.y < SQ_BLOCKSIZE - y_axis_max)
        )
    {
        T sum_acc = 0;

        for (long i = 0; i < ixs_len; i++ ){
            const long y = threadIdx.y + ixs_2d[i].y;
            const long x = threadIdx.x + ixs_2d[i].x;
            sum_acc += tile[y][x];
        }
        out[gindex] = sum_acc / (T)ixs_len;

    }
}

template<long ixs_len, long x_axis_min, long x_axis_max, long y_axis_min, long y_axis_max>
__global__
void big_tile_2d(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long row_len,
    const long col_len
    )
{
    const long waste_x = x_axis_min + x_axis_max;
    const long waste_y = y_axis_min + y_axis_max;
    const long block_offset_x = blockIdx.x*SQ_BLOCKSIZE;
    const long block_offset_y = blockIdx.y*SQ_BLOCKSIZE;
    const long gidx = block_offset_x + threadIdx.x;
    const long gidy = block_offset_y + threadIdx.y;
    const long gindex = gidy * row_len + gidx;
    const long max_x_ix = row_len - 1;
    const long max_y_ix = col_len - 1;

    const long shared_size_x = SQ_BLOCKSIZE + waste_x;
    const long shared_size_y = SQ_BLOCKSIZE + waste_y;
    __shared__ T tile[shared_size_y][shared_size_x];

    const long x_iters = (shared_size_x + (SQ_BLOCKSIZE-1)) / SQ_BLOCKSIZE;
    const long y_iters = (shared_size_y + (SQ_BLOCKSIZE-1)) / SQ_BLOCKSIZE;

    for(long i = 0; i < y_iters; i++){

        for(long j = 0; j < x_iters; j++){
            const long local_y = threadIdx.y + i*SQ_BLOCKSIZE;
            const long local_x = threadIdx.x + j*SQ_BLOCKSIZE;
            if(local_x < shared_size_x && local_y < shared_size_y){
                const long gx = BOUNDL( local_x + block_offset_x - x_axis_min, max_x_ix);
                const long gy = BOUNDL( local_y + block_offset_y - y_axis_min, max_y_ix);
                const long index = gy * row_len + gx;
                tile[local_y][local_x] = A[index];
            }
        }
    }
    __syncthreads();

    if((gidx < row_len) && (gidy < col_len))
    {
        T sum_acc = 0;

        for (long i = 0; i < ixs_len; i++ ){
            const long y = threadIdx.y + y_axis_min + ixs_2d[i].y;
            const long x = threadIdx.x + x_axis_min + ixs_2d[i].x;
            sum_acc += tile[y][x];
        }
        out[gindex] = sum_acc / (T)ixs_len;
    }
}
*/


/*
template<long x_axis_min, long x_axis_max, long y_axis_min, long y_axis_max>
__global__
void small_tile_2d_inline_reduce(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long row_len,
    const long col_len
    )
{
    __shared__ T tile[SQ_BLOCKSIZE][SQ_BLOCKSIZE];
    const long waste_x = x_axis_min + x_axis_max;
    const long waste_y = y_axis_min + y_axis_max;
    const long gidx = blockIdx.x*(SQ_BLOCKSIZE - waste_x) + threadIdx.x - x_axis_min;
    const long gidy = blockIdx.y*(SQ_BLOCKSIZE - waste_y) + threadIdx.y - y_axis_min;
    const long gindex = gidy * row_len + gidx;
    const long max_x_ix = row_len - 1;
    const long max_y_ix = col_len - 1;
    const long x = BOUNDL(gidx, max_x_ix);
    const long y = BOUNDL(gidy, max_y_ix);
    const long index = y * row_len + x;
    tile[threadIdx.y][threadIdx.x] = A[index];
    __syncthreads();

    if (    (0 <= gidx && gidx < row_len)
        &&  (x_axis_min <= threadIdx.x && threadIdx.x < SQ_BLOCKSIZE - x_axis_max)
        &&  (0 <= gidy && gidy < col_len)
        &&  (y_axis_min <= threadIdx.y && threadIdx.y < SQ_BLOCKSIZE - y_axis_max)
        )
    {
        const long x_range = x_axis_max + x_axis_min + 1;
        const long y_range = y_axis_max + y_axis_min + 1;
        const long total_range = x_range * y_range;
        T sum_acc = 0;

        for(long i=0; i < y_range; i++){
            for(long j=0; j < x_range; j++){
                const long y = threadIdx.y + (i - y_axis_min);
                const long x = threadIdx.x + (j - x_axis_min);
                sum_acc += tile[y][x];
            }
        }
        out[gindex] = sum_acc / (T)total_range;
    }
}

template<long x_axis_min, long x_axis_max, long y_axis_min, long y_axis_max>
__global__
void big_tile_2d_inline_reduce(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long row_len,
    const long col_len
    )
{
    const long waste_x = x_axis_max - x_axis_min; //this was changed
    const long waste_y = y_axis_max - y_axis_min; //this was changed
    const long block_offset_x = blockIdx.x*SQ_BLOCKSIZE;
    const long block_offset_y = blockIdx.y*SQ_BLOCKSIZE;
    const long gidx = block_offset_x + threadIdx.x;
    const long gidy = block_offset_y + threadIdx.y;
    const long gindex = gidy * row_len + gidx;
    const long max_x_ix = row_len - 1;
    const long max_y_ix = col_len - 1;

    const long shared_size_x = SQ_BLOCKSIZE + waste_x;
    const long shared_size_y = SQ_BLOCKSIZE + waste_y;
    __shared__ T tile[shared_size_y][shared_size_x];

    const long x_iters = (shared_size_x + (SQ_BLOCKSIZE-1)) / SQ_BLOCKSIZE;
    const long y_iters = (shared_size_y + (SQ_BLOCKSIZE-1)) / SQ_BLOCKSIZE;

    for(long i = 0; i < y_iters; i++){
        const long local_y = threadIdx.y + i*SQ_BLOCKSIZE;
        const long gy = BOUNDL( local_y + block_offset_y - y_axis_min, max_y_ix)
                     * row_len;

        for(long j = 0; j < x_iters; j++){
            const long local_x = threadIdx.x + j*SQ_BLOCKSIZE;
            const long gx = BOUNDL( local_x + block_offset_x - x_axis_min, max_x_ix);
            if(local_x < shared_size_x && local_y < shared_size_y){
                tile[local_y][local_x] = A[gx + gy];
            }
        }
    }
    __syncthreads();

    if((gidx < row_len) && (gidy < col_len))
    {
        const long x_range = x_axis_max + x_axis_min + 1;
        const long y_range = y_axis_max + y_axis_min + 1;
        const long total_range = x_range * y_range;
        T sum_acc = 0;

        for(long i=0; i < y_range; i++){

            for(long j=0; j < x_range; j++){
                const long y = threadIdx.y + i;
                const long x = threadIdx.x + j;
                sum_acc += tile[y][x];
            }
        }
        out[gindex] = sum_acc / (T)total_range;
    }
}


template<long x_axis_min, long x_axis_max, long y_axis_min, long y_axis_max>
__global__
void big_tile_2d_inline_reduce_flat(
    const T* __restrict__ A,
    T* __restrict__ out,
    const long row_len,
    const long col_len
    )
{
    const long waste_x = x_axis_min + x_axis_max;
    const long waste_y = y_axis_min + y_axis_max;
    const long block_offset_x = blockIdx.x*SQ_BLOCKSIZE;
    const long block_offset_y = blockIdx.y*SQ_BLOCKSIZE;
    const long gidx = block_offset_x + threadIdx.x;
    const long gidy = block_offset_y + threadIdx.y;
    const long gindex = gidy * row_len + gidx;
    const long max_x_ix = row_len - 1;
    const long max_y_ix = col_len - 1;

    const long shared_size_x = SQ_BLOCKSIZE + waste_x;
    const long shared_size_y = SQ_BLOCKSIZE + waste_y;

    const long flatIndex = threadIdx.y*SQ_BLOCKSIZE + threadIdx.x;

    const long shared_size = shared_size_x*shared_size_y;
    __shared__ T tile[shared_size];

    const long flatBlock = SQ_BLOCKSIZE*SQ_BLOCKSIZE;
    const long iters = CEIL_DIV(shared_size, flatBlock);


    for(long i = 0; i < iters; i++){
        const long local_ix = flatIndex + i*flatBlock;
        const long local_x = local_ix % shared_size_x;
        const long local_y = (local_ix / shared_size_x);

        const long gx = BOUNDL( local_x + block_offset_x - x_axis_min, max_x_ix);
        const long gy = BOUNDL( local_y + block_offset_y - y_axis_min, max_y_ix)
                     * row_len;

        if(local_ix < shared_size){
            tile[local_ix] = A[gx + gy];
        }
    }
    __syncthreads();

    if((gidx < row_len) && (gidy < col_len))
    {
        const long x_range = x_axis_max + x_axis_min + 1;
        const long y_range = y_axis_max + y_axis_min + 1;
        const long total_range = x_range * y_range;
        T sum_acc = 0;

        for(long i=0; i < y_range; i++){

            for(long j=0; j < x_range; j++){
                const long y = threadIdx.y + i;
                const long x = threadIdx.x + j;
                sum_acc += tile[shared_size_x*y + x];
            }
        }
        out[gindex] = sum_acc / (T)total_range;
    }
}*/



#endif

