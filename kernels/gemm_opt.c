#include "sse2neon.h"
void gemm_compute(const float *A, const float *B, float *C, int M, int N, int K) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {

            __m128 sum_vec = _mm_setzero_ps();
            int k = 0;

            for (; k <= K - 4; k += 4) {

                __m128 a_vec = _mm_loadu_ps(&A[i * K + k]);

                __m128 b_vec = _mm_set_ps(
                    B[(k + 3) * N + j],
                    B[(k + 2) * N + j],
                    B[(k + 1) * N + j],
                    B[(k + 0) * N + j]
                );

                #if defined(__FMA__)

                #else
                sum_vec = _mm_add_ps(sum_vec, _mm_mul_ps(a_vec, b_vec));
                #endif
            }
            
            __m128 shuf = _mm_shuffle_ps(sum_vec, sum_vec, _MM_SHUFFLE(2, 3, 0, 1));
            __m128 sums = _mm_add_ps(sum_vec, shuf);
            shuf = _mm_movehl_ps(shuf, sums);
            sums = _mm_add_ss(sums, shuf);
            float sum = _mm_cvtss_f32(sums);

            for (; k < K; k++) {
                sum += A[i * K + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}

  