// SPDX-FileCopyrightText: 2017 - 2024 The Ginkgo authors
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef GKO_CUDA_COMPONENTS_FORMAT_CONVERSION_CUH_
#define GKO_CUDA_COMPONENTS_FORMAT_CONVERSION_CUH_


#include <ginkgo/config.hpp>
#include <ginkgo/core/base/executor.hpp>


#include "cuda/components/cooperative_groups.cuh"
#include "cuda/components/thread_ids.cuh"


#ifdef GINKGO_BENCHMARK_ENABLE_TUNING
#include "benchmark/utils/tuning_variables.hpp"
#endif  // GINKGO_BENCHMARK_ENABLE_TUNING


namespace gko {
namespace kernels {
namespace cuda {
namespace ell {
namespace kernel {


/**
 * @internal
 *
 * It counts the number of explicit nonzeros per row of Ell.
 */
template <typename ValueType, typename IndexType>
__global__ void count_nnz_per_row(size_type num_rows, size_type max_nnz_per_row,
                                  size_type stride,
                                  const ValueType* __restrict__ values,
                                  IndexType* __restrict__ result);


}  // namespace kernel
}  // namespace ell


namespace coo {
namespace kernel {


/**
 * @internal
 *
 * It converts the row index of Coo to the row pointer of Csr.
 */
template <typename IndexType>
__global__ void convert_row_idxs_to_ptrs(const IndexType* __restrict__ idxs,
                                         size_type num_nonzeros,
                                         IndexType* __restrict__ ptrs,
                                         size_type length);


}  // namespace kernel


namespace host_kernel {


/**
 * @internal
 *
 * It calculates the number of warps used in Coo Spmv depending on the GPU
 * architecture and the number of stored elements.
 */
template <size_type subwarp_size = config::warp_size>
__host__ size_type calculate_nwarps(std::shared_ptr<const CudaExecutor> exec,
                                    const size_type nnz)
{
    size_type warps_per_sm =
        exec->get_num_warps_per_sm() * config::warp_size / subwarp_size;
    size_type nwarps_in_cuda = exec->get_num_multiprocessor() * warps_per_sm;
    size_type multiple = 8;
    if (nnz >= 2e8) {
        multiple = 2048;
    } else if (nnz >= 2e7) {
        multiple = 512;
    } else if (nnz >= 2e6) {
        multiple = 128;
    } else if (nnz >= 2e5) {
        multiple = 32;
    }
#ifdef GINKGO_BENCHMARK_ENABLE_TUNING
    if (_tuning_flag) {
        multiple = _tuned_value;
    }
#endif  // GINKGO_BENCHMARK_ENABLE_TUNING
    return std::min(multiple * nwarps_in_cuda,
                    size_type(ceildiv(nnz, config::warp_size)));
}


}  // namespace host_kernel
}  // namespace coo
}  // namespace cuda
}  // namespace kernels
}  // namespace gko


#endif  // GKO_CUDA_COMPONENTS_FORMAT_CONVERSION_CUH_