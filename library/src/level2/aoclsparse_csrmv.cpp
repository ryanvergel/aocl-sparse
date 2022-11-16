/* ************************************************************************
 * Copyright (c) 2020-2022 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ************************************************************************ */

#include "aoclsparse.h"
#include "aoclsparse_csrmv.hpp"

// Template specializations
template <>
aoclsparse_status aoclsparse_csrmv_vectorized(const float    alpha,
                                              aoclsparse_int m,
                                              aoclsparse_int n,
                                              aoclsparse_int nnz,
                                              const float *__restrict__ csr_val,
                                              const aoclsparse_int *__restrict__ csr_col_ind,
                                              const aoclsparse_int *__restrict__ csr_row_ptr,
                                              const float *__restrict__ x,
                                              const float beta,
                                              float *__restrict__ y,
                                              aoclsparse_context *context)
{
    __m256 vec_vals, vec_x, vec_y;

#ifdef _OPENMP
#pragma omp parallel for num_threads(context->num_threads) private(vec_vals, vec_x, vec_y)
#endif
    for(aoclsparse_int i = 0; i < m; i++)
    {
        aoclsparse_int j;
        float          result = 0.0;
        vec_y                 = _mm256_setzero_ps();
        aoclsparse_int nnz    = csr_row_ptr[i + 1] - csr_row_ptr[i];
        aoclsparse_int k_iter = nnz / 8;
        aoclsparse_int k_rem  = nnz % 8;

        //Loop in multiples of 8
        for(j = csr_row_ptr[i]; j < csr_row_ptr[i + 1] - k_rem; j += 8)
        {
            //(csr_val[j] csr_val[j+1] csr_val[j+2] csr_val[j+3] csr_val[j+4] csr_val[j+5] csr_val[j+6] csr_val[j+7]
            vec_vals = _mm256_loadu_ps(&csr_val[j]);

            //Gather the xvector values from the column indices
            vec_x = _mm256_set_ps(x[csr_col_ind[j + 7]],
                                  x[csr_col_ind[j + 6]],
                                  x[csr_col_ind[j + 5]],
                                  x[csr_col_ind[j + 4]],
                                  x[csr_col_ind[j + 3]],
                                  x[csr_col_ind[j + 2]],
                                  x[csr_col_ind[j + 1]],
                                  x[csr_col_ind[j]]);

            vec_y = _mm256_fmadd_ps(vec_vals, vec_x, vec_y);
        }

        // Horizontal addition of vec_y
        if(k_iter)
        {
            // hiQuad = ( x7, x6, x5, x4 )
            __m128 hiQuad = _mm256_extractf128_ps(vec_y, 1);
            // loQuad = ( x3, x2, x1, x0 )
            const __m128 loQuad = _mm256_castps256_ps128(vec_y);
            // sumQuad = ( x3 + x7, x2 + x6, x1 + x5, x0 + x4 )
            const __m128 sumQuad = _mm_add_ps(loQuad, hiQuad);
            // loDual = ( -, -, x1 + x5, x0 + x4 )
            const __m128 loDual = sumQuad;
            // hiDual = ( -, -, x3 + x7, x2 + x6 )
            const __m128 hiDual = _mm_movehl_ps(sumQuad, sumQuad);
            // sumDual = ( -, -, x1 + x3 + x5 + x7, x0 + x2 + x4 + x6 )
            const __m128 sumDual = _mm_add_ps(loDual, hiDual);
            // lo = ( -, -, -, x0 + x2 + x4 + x6 )
            const __m128 lo = sumDual;
            // hi = ( -, -, -, x1 + x3 + x5 + x7 )
            const __m128 hi = _mm_shuffle_ps(sumDual, sumDual, 0x1);
            // sum = ( -, -, -, x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 )
            const __m128 sum = _mm_add_ss(lo, hi);
            result           = _mm_cvtss_f32(sum);
        }

        //Remainder loop
        for(j = csr_row_ptr[i + 1] - k_rem; j < csr_row_ptr[i + 1]; j++)
        {
            result += csr_val[j] * x[csr_col_ind[j]];
        }

        // Perform alpha * A * x
        if(alpha != static_cast<float>(1))
        {
            result = alpha * result;
        }

        // Perform (beta * y) + (alpha * A * x)
        if(beta != static_cast<float>(0))
        {
            result += beta * y[i];
        }

        y[i] = result;
    }

    return aoclsparse_status_success;
}

#if USE_AVX512
template <>
aoclsparse_status aoclsparse_csrmv_vectorized_avx512(const double   alpha,
                                                     aoclsparse_int m,
                                                     aoclsparse_int n,
                                                     aoclsparse_int nnz,
                                                     const double *__restrict__ csr_val,
                                                     const aoclsparse_int *__restrict__ csr_col_ind,
                                                     const aoclsparse_int *__restrict__ csr_row_ptr,
                                                     const double *__restrict__ x,
                                                     const double beta,
                                                     double *__restrict__ y,
                                                     aoclsparse_context *context)
{
    __m256d vec_y;
    __m512d vec_vals_512, vec_x_512, vec_y_512;
    for(aoclsparse_int i = 0; i < m; i++)
    {
        aoclsparse_int j;
        double         result = 0.0;
        aoclsparse_int nnz    = csr_row_ptr[i + 1] - csr_row_ptr[i];
        aoclsparse_int k_iter = nnz / 8;
        aoclsparse_int k_rem  = nnz % 8;

        vec_y_512 = _mm512_setzero_pd();

        // Loop in multiples of 8 non-zeroes
        for(j = csr_row_ptr[i]; j < (csr_row_ptr[i + 1] - k_rem); j += 8)
        {
            //(csr_val[j] (csr_val[j+1] (csr_val[j+2] (csr_val[j+3]
            vec_vals_512 = _mm512_loadu_pd((double const *)&csr_val[j]);

            // Gather the x vector elements from the column indices
            vec_x_512 = _mm512_set_pd(x[csr_col_ind[j + 7]],
                                      x[csr_col_ind[j + 6]],
                                      x[csr_col_ind[j + 5]],
                                      x[csr_col_ind[j + 4]],
                                      x[csr_col_ind[j + 3]],
                                      x[csr_col_ind[j + 2]],
                                      x[csr_col_ind[j + 1]],
                                      x[csr_col_ind[j]]);

            vec_y_512 = _mm512_fmadd_pd(vec_vals_512, vec_x_512, vec_y_512);
        }
        vec_y = _mm256_add_pd(_mm512_extractf64x4_pd(vec_y_512, 0x0),
                              _mm512_extractf64x4_pd(vec_y_512, 0x1));

        if(k_iter)
        {

            // sum[0] += sum[1] ; sum[2] += sum[3]
            vec_y = _mm256_hadd_pd(vec_y, vec_y);
            // Cast avx_sum to 128 bit to obtain sum[0] and sum[1]
            __m128d sum_lo = _mm256_castpd256_pd128(vec_y);
            // Extract 128 bits to obtain sum[2] and sum[3]
            __m128d sum_hi = _mm256_extractf128_pd(vec_y, 1);

            // Add remaining two sums
            __m128d sse_sum = _mm_add_pd(sum_lo, sum_hi);

            // Store result
            /*
           __m128d in gcc is typedef as double
           but in Windows, this is defined as a struct
           */
#if !defined(__clang__) && (defined(_WIN32) || defined(_WIN64))
            result = sse_sum.m128d_f64[0];
#else
            result = sse_sum[0];
#endif
        }
        // Remainder loop for nnz%8
        for(j = csr_row_ptr[i + 1] - k_rem; j < csr_row_ptr[i + 1]; j++)
        {
            result += csr_val[j] * x[csr_col_ind[j]];
        }

        // Perform alpha * A * x
        if(alpha != static_cast<double>(1))
        {
            result = alpha * result;
        }

        // Perform (beta * y) + (alpha * A * x)
        if(beta != static_cast<double>(0))
        {
            result += beta * y[i];
        }

        y[i] = result;
    }
    return aoclsparse_status_success;
}

#endif

template <>
aoclsparse_status aoclsparse_csrmv_vectorized_avx2(const double   alpha,
                                                   aoclsparse_int m,
                                                   aoclsparse_int n,
                                                   aoclsparse_int nnz,
                                                   const double *__restrict__ csr_val,
                                                   const aoclsparse_int *__restrict__ csr_col_ind,
                                                   const aoclsparse_int *__restrict__ csr_row_ptr,
                                                   const double *__restrict__ x,
                                                   const double beta,
                                                   double *__restrict__ y,
                                                   aoclsparse_context *context)
{

    __m256d vec_vals, vec_x, vec_y;
#ifdef _OPENMP
#pragma omp parallel for num_threads(context->num_threads) \
    schedule(dynamic, m / context->num_threads) private(vec_vals, vec_x, vec_y)
#endif
    for(aoclsparse_int i = 0; i < m; i++)
    {
        aoclsparse_int j;
        double         result = 0.0;
        vec_y                 = _mm256_setzero_pd();
        aoclsparse_int nnz    = csr_row_ptr[i + 1] - csr_row_ptr[i];
        aoclsparse_int k_iter = nnz / 4;
        aoclsparse_int k_rem  = nnz % 4;

        //Loop in multiples of 4 non-zeroes
        for(j = csr_row_ptr[i]; j < csr_row_ptr[i + 1] - k_rem; j += 4)
        {
            //(csr_val[j] (csr_val[j+1] (csr_val[j+2] (csr_val[j+3]
            vec_vals = _mm256_loadu_pd((double const *)&csr_val[j]);

            //Gather the x vector elements from the column indices
            vec_x = _mm256_set_pd(x[csr_col_ind[j + 3]],
                                  x[csr_col_ind[j + 2]],
                                  x[csr_col_ind[j + 1]],
                                  x[csr_col_ind[j]]);

            vec_y = _mm256_fmadd_pd(vec_vals, vec_x, vec_y);
        }

        // Horizontal addition
        if(k_iter)
        {
            // sum[0] += sum[1] ; sum[2] += sum[3]
            vec_y = _mm256_hadd_pd(vec_y, vec_y);
            // Cast avx_sum to 128 bit to obtain sum[0] and sum[1]
            __m128d sum_lo = _mm256_castpd256_pd128(vec_y);
            // Extract 128 bits to obtain sum[2] and sum[3]
            __m128d sum_hi = _mm256_extractf128_pd(vec_y, 1);
            // Add remaining two sums
            __m128d sse_sum = _mm_add_pd(sum_lo, sum_hi);
            // Store result
            /*
	       __m128d in gcc is typedef as double
	       but in Windows, this is defined as a struct
	       */
#if !defined(__clang__) && (defined(_WIN32) || defined(_WIN64))
            result = sse_sum.m128d_f64[0];
#else
            result = sse_sum[0];
#endif
        }

        //Remainder loop for nnz%4
        for(j = csr_row_ptr[i + 1] - k_rem; j < csr_row_ptr[i + 1]; j++)
        {
            result += csr_val[j] * x[csr_col_ind[j]];
        }

        // Perform alpha * A * x
        if(alpha != static_cast<double>(1))
        {
            result = alpha * result;
        }

        // Perform (beta * y) + (alpha * A * x)
        if(beta != static_cast<double>(0))
        {
            result += beta * y[i];
        }

        y[i] = result;
    }
    return aoclsparse_status_success;
}

/*
 *===========================================================================
 *   C wrapper
 * ===========================================================================
 */
extern "C" aoclsparse_status aoclsparse_scsrmv(aoclsparse_operation       trans,
                                               const float               *alpha,
                                               aoclsparse_int             m,
                                               aoclsparse_int             n,
                                               aoclsparse_int             nnz,
                                               const float               *csr_val,
                                               const aoclsparse_int      *csr_col_ind,
                                               const aoclsparse_int      *csr_row_ptr,
                                               const aoclsparse_mat_descr descr,
                                               const float               *x,
                                               const float               *beta,
                                               float                     *y)
{
    // Read the environment variables to update global variable
    // This function updates the num_threads only once.
    aoclsparse_init_once();

    aoclsparse_context context;
    context.num_threads = global_context.num_threads;

    if(descr == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }

    // Check index base
    if(descr->base != aoclsparse_index_base_zero)
    {
        // TODO
        return aoclsparse_status_not_implemented;
    }

    // Support General and symmetric matrices.
    // Return for any other matrix type
    if((descr->type != aoclsparse_matrix_type_general)
       && (descr->type != aoclsparse_matrix_type_symmetric))
    {
        // TODO
        return aoclsparse_status_not_implemented;
    }

    if(trans != aoclsparse_operation_none)
    {
        // TODO
        return aoclsparse_status_not_implemented;
    }

    // Check sizes
    if(m < 0)
    {
        return aoclsparse_status_invalid_size;
    }
    else if(n < 0)
    {
        return aoclsparse_status_invalid_size;
    }
    else if(nnz < 0)
    {
        return aoclsparse_status_invalid_size;
    }

    // Quick return if possible
    if(m == 0 || n == 0 || nnz == 0)
    {
        return aoclsparse_status_success;
    }

    // Check pointer arguments
    if(csr_val == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    else if(csr_row_ptr == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    else if(csr_col_ind == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    else if(x == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    else if(y == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    if(descr->type == aoclsparse_matrix_type_symmetric)
    {
        return aoclsparse_csrmv_symm(
            *alpha, m, n, nnz, csr_val, csr_col_ind, csr_row_ptr, x, *beta, y);
    }
    else
    {
        return aoclsparse_csrmv_vectorized(
            *alpha, m, n, nnz, csr_val, csr_col_ind, csr_row_ptr, x, *beta, y, &context);
    }
}

extern "C" aoclsparse_status aoclsparse_dcsrmv(aoclsparse_operation       trans,
                                               const double              *alpha,
                                               aoclsparse_int             m,
                                               aoclsparse_int             n,
                                               aoclsparse_int             nnz,
                                               const double              *csr_val,
                                               const aoclsparse_int      *csr_col_ind,
                                               const aoclsparse_int      *csr_row_ptr,
                                               const aoclsparse_mat_descr descr,
                                               const double              *x,
                                               const double              *beta,
                                               double                    *y)
{
    // Read the environment variables to update global variable
    // This function updates the num_threads only once.
    aoclsparse_init_once();

    aoclsparse_context context;
    context.num_threads = global_context.num_threads;
    context.is_avx512   = global_context.is_avx512;

    if(descr == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }

    // Check index base
    if(descr->base != aoclsparse_index_base_zero)
    {
        // TODO
        return aoclsparse_status_not_implemented;
    }

    // Support General and symmetric matrices.
    // Return for any other matrix type
    if((descr->type != aoclsparse_matrix_type_general)
       && (descr->type != aoclsparse_matrix_type_symmetric))
    {
        // TODO
        return aoclsparse_status_not_implemented;
    }

    if(trans != aoclsparse_operation_none)
    {
        // TODO
        return aoclsparse_status_not_implemented;
    }

    // Check sizes
    if(m < 0)
    {
        return aoclsparse_status_invalid_size;
    }
    else if(n < 0)
    {
        return aoclsparse_status_invalid_size;
    }
    else if(nnz < 0)
    {
        return aoclsparse_status_invalid_size;
    }

    // Quick return if possible
    if(m == 0 || n == 0 || nnz == 0)
    {
        return aoclsparse_status_success;
    }

    // Check pointer arguments
    if(csr_val == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    else if(csr_row_ptr == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    else if(csr_col_ind == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    else if(x == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    else if(y == nullptr)
    {
        return aoclsparse_status_invalid_pointer;
    }
    if(descr->type == aoclsparse_matrix_type_symmetric)
    {
        return aoclsparse_csrmv_symm(
            *alpha, m, n, nnz, csr_val, csr_col_ind, csr_row_ptr, x, *beta, y);
    }
    else
    {
        // Sparse matrices with Mean nnz = nnz/m <10 have very few non-zeroes in most of the rows
        // and few unevenly long rows . Loop unrolling and vectorization doesnt optimise performance
        // for this category of matrices . Hence , we invoke the generic dcsrmv kernel without
        // vectorization and innerloop unrolling . For the other category of sparse matrices
        // (Mean nnz > 10) , we continue to invoke the vectorised version of csrmv , since
        // it improves performance.
        if(nnz <= (10 * m))
            return aoclsparse_csrmv_general(
                *alpha, m, n, nnz, csr_val, csr_col_ind, csr_row_ptr, x, *beta, y, &context);
        else
        {
#if USE_AVX512
            if(context.is_avx512)
                return aoclsparse_csrmv_vectorized_avx512(
                    *alpha, m, n, nnz, csr_val, csr_col_ind, csr_row_ptr, x, *beta, y, &context);
            else
                return aoclsparse_csrmv_vectorized_avx2(
                    *alpha, m, n, nnz, csr_val, csr_col_ind, csr_row_ptr, x, *beta, y, &context);
#else
            return aoclsparse_csrmv_vectorized_avx2(
                *alpha, m, n, nnz, csr_val, csr_col_ind, csr_row_ptr, x, *beta, y, &context);
#endif
        }
    }
}
