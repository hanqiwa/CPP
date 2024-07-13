#include "norm.hpp"

template <bool vals_smem, int ncols_template, int block_size_template>
static void soft_max_f32(const float * x, const float * mask, float * dst, const int ncols_par,
                         const int nrows_y, const float scale, const float max_bias, const float m0,
                         const float m1, uint32_t n_head_log2, const sycl::nd_item<3> &item_ct1, float *buf) {
    const int ncols = ncols_template == 0 ? ncols_par : ncols_template;

    const int tid = item_ct1.get_local_id(2);
    const int rowx = item_ct1.get_group(2);
    const int rowy = rowx % nrows_y; // broadcast the mask (y) in the row dimension

    const int block_size = block_size_template == 0 ? item_ct1.get_local_range(2) : block_size_template;

    const int warp_id = item_ct1.get_local_id(2) / WARP_SIZE;
    const int lane_id = item_ct1.get_local_id(2) % WARP_SIZE;
    const int nthreads = block_size;
    const int nwarps = nthreads / WARP_SIZE;
    int nreduce = nwarps / WARP_SIZE;
    float slope = 1.0f;

    // ALiBi
    if (max_bias > 0.0f) {
        const uint32_t h = rowx/nrows_y; // head index

        const float base = h < n_head_log2 ? m0 : m1;
        const int   exp  = h < n_head_log2 ? h + 1 : 2*(h - n_head_log2) + 1;

        slope = sycl::pow(base, float(exp));
    }

    float *vals = vals_smem ? buf + std::max(nwarps, WARP_SIZE) : dst + rowx * ncols;
    float max_val = -INFINITY;

    for (int col0 = 0; col0 < ncols; col0 += block_size) {
        const int col = col0 + tid;

        if (ncols_template == 0 && col >= ncols) {
            break;
        }

        const int ix = rowx*ncols + col;
        const int iy = rowy*ncols + col;

        const float val = x[ix]*scale + (mask ? slope*mask[iy] : 0.0f);

        vals[col] = val;
        max_val = sycl::max(max_val, val);
    }

    // find the max value in the block
    max_val = warp_reduce_max(max_val, item_ct1);
    if (block_size > WARP_SIZE) {
        if (warp_id == 0) {
            buf[lane_id] = -INFINITY;
            for (size_t i = 1; i < nreduce; i += 1)
                buf[lane_id + i * WARP_SIZE] = -INFINITY;
        }
        item_ct1.barrier(sycl::access::fence_space::local_space);

        if (lane_id == 0) {
            buf[warp_id] = max_val;
        }
        item_ct1.barrier(sycl::access::fence_space::local_space);
        max_val = buf[lane_id];
        for (size_t i = 1; i < nreduce; i += 1)
        {
            max_val = std::max(max_val, buf[lane_id + i * WARP_SIZE]);
        }
        max_val = warp_reduce_max(max_val, item_ct1);
    }

    float tmp = 0.f;
#pragma unroll
    for (int col0 = 0; col0 < ncols; col0 += block_size) {
        const int col = col0 + tid;
                if (ncols_template == 0 && col >= ncols) {
            break;
        }

        const float val = sycl::native::exp(vals[col] - max_val);
        tmp += val;
        vals[col] = val;
    }

    // find the sum of exps in the block
    tmp = warp_reduce_sum(tmp, item_ct1);
    if (block_size > WARP_SIZE) {
        item_ct1.barrier(sycl::access::fence_space::local_space);
        if (warp_id == 0) {
            buf[lane_id] = 0.f;
            for (size_t i = 1; i < nreduce; i += 1)
                buf[lane_id + i * WARP_SIZE] = 0.f;
        }
        item_ct1.barrier(sycl::access::fence_space::local_space);

        if (lane_id == 0) {
            buf[warp_id] = tmp;
        }
        item_ct1.barrier(sycl::access::fence_space::local_space);

        tmp = buf[lane_id];
        for (size_t i = 1; i < nreduce; i += 1)
        {
            tmp += buf[lane_id + i * WARP_SIZE];
        }
        tmp = warp_reduce_sum(tmp, item_ct1);
    }

    const float inv_sum = 1.f / tmp;

#pragma unroll
    for (int col0 = 0; col0 < ncols; col0 += block_size) {
        const int col = col0 + tid;

        if (ncols_template == 0 && col >= ncols) {
            return;
        }

        const int idst = rowx*ncols + col;
        dst[idst] = vals[col] * inv_sum;
    }
}

template <bool vals_smem, int ncols_template, int block_size_template>
static void soft_max_f32_submitter(const float * x, const float * mask, float * dst, const int ncols_par,
                                   const int nrows_y, const float scale, const float max_bias, const float m0,
                                   const float m1, uint32_t n_head_log2, sycl::range<3> block_nums, sycl::range<3> block_dims,
                                   const size_t n_local_scratch, queue_ptr stream) {
    stream->submit([&](sycl::handler &cgh) {
        sycl::local_accessor<float, 1> local_buf_acc(n_local_scratch, cgh);

        cgh.parallel_for(
            sycl::nd_range<3>(block_nums * block_dims, block_dims),
            [=](sycl::nd_item<3> item_ct1) [[intel::reqd_sub_group_size(WARP_SIZE)]] {
                soft_max_f32<vals_smem, ncols_template, block_size_template>(x, mask, dst, ncols_par,
                                                                             nrows_y, scale, max_bias, m0,
                                                                             m1, n_head_log2, item_ct1,
                                                                             get_pointer(local_buf_acc));
            });
    });
}

static void soft_max_f32_sycl(const float * x, const float * mask,
                              float * dst, const int ncols_x, const int nrows_x,
                              const int nrows_y, const float scale, const float max_bias,
                              queue_ptr stream, int device_id) {
    int nth = WARP_SIZE;
    int max_block_size = ggml_sycl_info().work_group_size(device_id);
    while (nth < ncols_x && nth < max_block_size) nth *= 2;
    if (nth>max_block_size) nth = max_block_size;

    const sycl::range<3> block_dims(1, 1, nth);
    const sycl::range<3> block_nums(1, 1, nrows_x);
    const size_t n_local_scratch = (GGML_PAD(ncols_x, WARP_SIZE) + WARP_SIZE);

    const uint32_t n_head_kv   = nrows_x/nrows_y;
    const uint32_t n_head_log2 = 1u << (uint32_t) floorf(log2f((float) n_head_kv));

    const float m0 = powf(2.0f, -(max_bias       ) / n_head_log2);
    const float m1 = powf(2.0f, -(max_bias / 2.0f) / n_head_log2);

    const size_t local_mem_size = stream->get_device().get_info<sycl::info::device::local_mem_size>();
    if (n_local_scratch*sizeof(float) < local_mem_size) {
        if (ncols_x > max_block_size) {
            soft_max_f32_submitter<true, 0, 0>(x, mask, dst, ncols_x, nrows_y, scale,
                                               max_bias, m0, m1, n_head_log2, block_nums,
                                               block_dims, n_local_scratch, stream);
            return;
        }
        switch (ncols_x) {
            case 32:
                soft_max_f32_submitter<true, 32, 32>(x, mask, dst, ncols_x, nrows_y, scale,
                                                     max_bias, m0, m1, n_head_log2, block_nums,
                                                     block_dims, n_local_scratch, stream);
                break;
            case 64:
                soft_max_f32_submitter<true, 64, 64>(x, mask, dst, ncols_x, nrows_y, scale,
                                                     max_bias, m0, m1, n_head_log2, block_nums,
                                                     block_dims, n_local_scratch, stream);
                break;
            case 128:
                soft_max_f32_submitter<true, 128, 128>(x, mask, dst, ncols_x, nrows_y, scale,
                                                       max_bias, m0, m1, n_head_log2, block_nums,
                                                       block_dims, n_local_scratch, stream);
                break;
            case 256:
                soft_max_f32_submitter<true, 256, 256>(x, mask, dst, ncols_x, nrows_y, scale,
                                                       max_bias, m0, m1, n_head_log2, block_nums,
                                                       block_dims, n_local_scratch, stream);
                break;
            case 512:
                soft_max_f32_submitter<true, 512, 512>(x, mask, dst, ncols_x, nrows_y, scale,
                                                       max_bias, m0, m1, n_head_log2, block_nums,
                                                       block_dims, n_local_scratch, stream);
                break;
            case 1024:
                soft_max_f32_submitter<true, 1024, 1024>(x, mask, dst, ncols_x, nrows_y, scale,
                                                         max_bias, m0, m1, n_head_log2, block_nums,
                                                         block_dims, n_local_scratch, stream);
                break;
            case 2048:
                soft_max_f32_submitter<true, 2048, 1024>(x, mask, dst, ncols_x, nrows_y, scale,
                                                         max_bias, m0, m1, n_head_log2, block_nums,
                                                         block_dims, n_local_scratch, stream);
                break;
            case 4096:
                soft_max_f32_submitter<true, 4096, 1024>(x, mask, dst, ncols_x, nrows_y, scale,
                                                         max_bias, m0, m1, n_head_log2, block_nums,
                                                         block_dims, n_local_scratch, stream);
                break;
            default:
                soft_max_f32_submitter<true, 0, 0>(x, mask, dst, ncols_x, nrows_y, scale,
                                                   max_bias, m0, m1, n_head_log2, block_nums,
                                                   block_dims, n_local_scratch, stream);
                break;
        }
    } else {
        soft_max_f32_submitter<false, 0, 0>(x, mask, dst, ncols_x, nrows_y, scale,
                                            max_bias, m0, m1, n_head_log2, block_nums,
                                            block_dims, WARP_SIZE, stream);
    }
}

void ggml_sycl_op_soft_max(ggml_backend_sycl_context & ctx, const ggml_tensor *src0,
                                  const ggml_tensor *src1, ggml_tensor *dst,
                                  const float *src0_dd, const float *src1_dd,
                                  float *dst_dd,
                                  const queue_ptr &main_stream) {

    GGML_ASSERT(src0->type == GGML_TYPE_F32);
    GGML_ASSERT( dst->type == GGML_TYPE_F32);

#pragma message("TODO: add ggml_sycl_op_soft_max() F16 src1 support")
#pragma message("ref:  https://github.com/ggerganov/llama.cpp/pull/5021")
    GGML_ASSERT(!src1 || src1->type == GGML_TYPE_F32); // src1 contains mask and it is optional

    const int64_t ne00 = src0->ne[0];
    const int64_t nrows_x = ggml_nrows(src0);
    const int64_t nrows_y = src0->ne[1];

    float scale = 1.0f;
    float max_bias = 0.0f;

    memcpy(&scale, dst->op_params + 0, sizeof(float));
    memcpy(&max_bias, dst->op_params + 1, sizeof(float));

    soft_max_f32_sycl(src0_dd, src1 ? src1_dd : nullptr, dst_dd, ne00,
        nrows_x, nrows_y, scale, max_bias, main_stream, ctx.device);
}
