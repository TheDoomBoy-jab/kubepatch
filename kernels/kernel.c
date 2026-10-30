/*kernel apps for three llm functions*/

#include <math.h>

void gemm_naive(const float *A, const float *B, float *C, int M, int N, int K) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++) {
                sum += A[i * K + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}

void layernorm(const float *x, float *out, int N, float eps) {
    float sum = 0.0f;
    for (int i = 0; i < N; i++) {
        sum += x[i];
    }
    float mean = sum / (float)N;

    float variance_sum = 0.0f;
    for (int i = 0; i < N; i++) {
        float diff = x[i] - mean;
        variance_sum += diff * diff;
    }
    float inv_std = 1.0f / sqrtf(variance_sum / (float)N + eps);

    for (int i = 0; i < N; i++) {
        out[i] = (x[i] - mean) * inv_std;
    }
}


void vector_relu(const float *x, float *out, int N) {
    for (int i = 0; i < N; i++) {
        out[i] = (x[i] > 0.0f) ? x[i] : 0.0f;
    }
}