# KubePatch: Low-Latency GPU Micro-Scheduler & In-Memory JIT Binary Linker

A high-throughput, sub-microsecond micro-workload scheduler and in-memory binary linker written from scratch in C11. Built to eliminate driver-level context-switching, IPC overhead, and runtime JIT compilation lag across concurrent multi-tenant container workloads.

---

## The Problem: Accelerator Contention in Container Runtimes

In multi-tenant AI inference clusters (8–16 pods running LLMs, embeddings, and vision models on a shared accelerator):
1. **Hardware Context Switching (10–50 µs/switch)**: Multiple isolated container processes cause the driver to flush registers, invalidate caches, and switch contexts.
2. **IPC Overhead (5–15 ms/request)**: Standard network and Unix sockets serialize descriptors and copy tensor memory across process boundaries.
3. **Cold-Start JIT Compilation (50–200 ms lag)**: Dynamic shape changes force heavy driver compilations or runtime shared-library loading.

KubePatch introduces a central host daemon that manages a single persistent execution context, communicates with tenant pods via lock-free shared memory (`/dev/shm`), and hot-patches compute kernels directly into executable RAM in sub-microsecond time.

---

## Architecture

```
   [ K8s Pod 1 ]       [ K8s Pod 2 ]  ...  [ K8s Pod 16 ]
         │                   │                   │
         └─────────┬─────────┴──────────┬────────┘
                   │ 
                   ▼ /dev/shm/kubepatch_shm (POSIX Shared Memory Volume)
   ┌─────────────────────────────────────────────────────────────┐
   │ Shared Control Block:                                       │
   │  1. Lock-Free MPMC Ring Buffer (64-byte TaskDescriptors)    │
   │  2. Contiguous 64 MB Tensor Arena (A, B, C Buffer Offsets)  │
   └─────────────────────────────┬───────────────────────────────┘
                                 │
                                 ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ KubePatch Host Daemon (Single Persistent Execution Context) │
   │  - Lock-free atomic CAS task ingestion                      │
   │  - Dynamic Symbol Registry (Weak vs. Strong Linker Rules)   │
   │  - In-Memory Mach-O JIT Relocation & Code Patcher           │
   │  - Zero-Copy Tensor Execution (GEMM, LayerNorm, ReLU)       │
   └─────────────────────────────────────────────────────────────┘
```

---

## Core Engineering Components

### 1. In-Memory Mach-O / ELF Loader & W^X JIT Patcher
* Parses 64-bit binary headers (`mach_header_64`, `load_command`, `segment_command_64`, `LC_SYMTAB`) directly from raw RAM buffers using `mmap`.
* Extracts compiled machine instructions (`__TEXT,__text`) and calculates relocation offsets dynamically.
* Enforces strict W^X security: allocates writable anonymous memory, copies opcodes, applies `mprotect(PROT_READ | PROT_EXEC)`, and invalidates CPU instruction caches (`sys_icache_invalidate`) to guarantee cache coherency across ARM64 cores.

### 2. Dynamic Symbol Registry & In-Flight Kernel Hot-Patching
* Implements standard static linker precedence rules in user-space:
  * Weak symbols (`__attribute__((weak))` / `N_WEAK_DEF`): Registered as fallbacks.
  * Strong symbols: Automatically overwrite and upgrade weak symbols without stopping the execution loop.
* Enables zero-downtime hot-swapping: dynamically replaces scalar kernels with vectorized SIMD kernels (`sse2neon.h` / NEON) mid-flight, doubling throughput under live traffic.

### 3. Lock-Free Multi-Producer Multi-Consumer (MPMC) Queue
* Based on Dmitry Vyukov's bounded MPMC algorithm using C11 atomics (`stdatomic.h`).
* Slot-level sequence counters with atomic acquire and release memory fences eliminate mutexes from the fast path.
* 64-byte cacheline padding prevents false sharing on the CPU memory bus.

### 4. Zero-Copy Shared Arena Allocator
* Eliminates heap allocator mutex contention across concurrent pods.
* Pre-allocates a 64 MB shared memory segment (`/dev/shm`). Pods perform lock-free atomic bump-allocations (`atomic_compare_exchange_weak`) with 64-byte alignment for SIMD registers.

---

## Empirical Benchmarks

Tested on Apple Silicon (ARM64) under full load:

| Metric | Measurement |
| :--- | :--- |
| **Concurrent Tenant Pods** | 16 isolated OS processes |
| **Total Workload** | 160,000 mixed tensor tasks (GEMM, LayerNorm, ReLU) |
| **Total Wall-Clock Time** | ~0.30 seconds |
| **Per-Pod Submission Rate** | 31,000 – 52,600 tasks/second |
| **Aggregate Dispatch Throughput** | ~500,000+ tasks/second |
| **P50 Latency (Median)** | < 400 nanoseconds |
| **P99 Latency (Tail)** | < 1.2 microseconds |
| **Hot-Patch Speedup** | 2.24x faster (Scalar to SIMD live upgrade) |

---

## Codebase Structure

```text
kubepatch/
├── include/
│   ├── arena.h
│   ├── loader.h
│   ├── patcher.h
│   ├── ringbuffer.h
│   ├── shm_context.h
│   ├── symbol_table.h
│   └── task.h
├── src/
│   ├── core/
│   │   ├── arena.c
│   │   ├── daemon.c
│   │   └── ringbuffer.c
│   ├── client/
│   │   └── pod_client.c
│   └── linker/
│       ├── macho_loader.c
│       ├── patcher.c
│       └── symbol_table.c
├── kernels/
│   ├── gemm_fallback.c
│   ├── gemm_opt.c
│   └── kernel.c
├── benchmarks/
│   └── bench_cluster.c
├── run_k8s_pods.sh
└── Makefile
```

---

## Quickstart

### Prerequisites
* macOS (ARM64) or Linux (x86_64)
* Clang / GCC with C11 support and POSIX threads (`-pthread`)

### 1. Build
```bash
git clone https://github.com/<your-username>/kubepatch.git
cd kubepatch
make clean && make
```

### 2. Run the 16-Pod Multi-Process Benchmark

Terminal 1 (Host Daemon):
```bash
./kubepatch_daemon
```

Terminal 2 (16 Concurrent Pods):
```bash
./run_k8s_pods.sh
```

---

## License
Distributed under the MIT License.
