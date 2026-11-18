# KubePatch speaker guide

## Opening

“KubePatch is a C runtime for small matrix jobs on the CPU. It keeps each job’s data in a reusable memory slot and puts the job description on a queue. During the benchmark, workers start with a fallback matrix multiply. The runtime then loads a SIMD version and updates the function address that workers look up, while the process keeps running.”

A **kernel** is a numerical routine in this project. **GEMM** means general matrix multiplication. **SIMD** means one instruction operates on multiple data lanes.

## Slide-by-slide talk track

| Slide | Time | What to say |
|---|---:|---|
| 1. KubePatch | 20 s | “I wanted to see what it takes to hand off small CPU jobs and change their implementation while the program is live. Matrix multiplication gives me two versions of the same operation to demonstrate that.” |
| 2. Dispatching small matrix jobs | 35 s | “Small jobs still need to get from a producer to a worker. I keep the matrices in one memory area and queue only their offsets and the job metadata. Looking up the function by name lets the code address change while the job format stays stable.” |
| 3. Task flow | 50 s | “A producer owns a slot containing its inputs and output. The worker pops the descriptor, validates it, and calls GEMM. It publishes completion atomically. The producer verifies the result before the slot can be reused. Byte offsets work even when two processes map the memory at different virtual addresses.” |
| 4. Live swap | 55 s | “Both versions are already compiled. The Mach-O loader finds each function’s offset in the code section. The runtime maps the code, changes its pages from writable to executable, and invalidates the instruction cache. The strong version replaces the weak entry atomically. If a worker has already selected the old function, it can finish because that code stays mapped.” |
| 5. Optimizations | 45 s | “The queue reuses slots and coordinates threads with atomic operations. The arena reuses input and output buffers. The SIMD kernel computes four float products per inner-loop iteration. Each addresses a different part of the cost.” |
| 6. Tech stack | 25 s | “The project uses C, Clang and Make on Apple silicon. Pthreads run the benchmark threads. POSIX shared memory connects the daemon and local clients. Mach-O provides the kernel object files and ARM NEON provides the vector operations.” |
| 7. Benchmark | 55 s | “Eight producer threads and two workers run 200,000 matrix jobs in one process. The harness verifies every output and reports how many calls went to each implementation. In these three runs per mode, the SIMD-only mode had higher whole-runtime throughput than fallback-only. This includes preparation, scheduling and verification. The latency clock covers submission through worker dispatch.” |
| 8. Live demo | 45 s | “I’ll run mixed mode and show the registry update, the call counts and the number of verified jobs. The project also has a separate daemon demo where 16 local client processes submitted work through shared memory and verified all 960 outputs.” |

Allow about **5½ minutes** before running the live commands or taking questions. Use first person only when it reflects your contribution.

## How it works

**Queue and buffer ownership.** The ring supports multiple producers and consumers. A compare-and-swap claims a position, and each slot has a sequence number to tell producers and workers when it can be used again. Release/acquire operations publish task data. The separate arena has 256 reusable task buffers. A worker publishes completion only after writing output. A producer checks the output before reusing storage. Queue operations copy descriptors, while the tensor bytes stay in the arena.

**Kernel loading and execution.** Clang builds trusted, self-contained ARM64 Mach-O object files. The loader checks the header and sections, rejects text relocations, and finds the requested symbol inside `__text`. The code gets copied into a writable mapping, which is switched to read/execute before the instruction cache is invalidated. This is runtime loading of precompiled code. The experiment does not implement a general linker or JIT compiler.

**Kernel selection.** The fallback has weak binding and the NEON implementation has strong binding. Registration publishes the strong function address with a release store. Worker lookups use an acquire load. Writers use a mutex. Workers keep running during the change, and both images remain mapped until the threads exit.

**SIMD.** GEMM computes each output value by multiplying elements from a row of A and a column of B and summing them. The vector path uses four float lanes at once, reduces their partial sums, and runs a scalar loop for leftover elements. The implementation uses SSE-style intrinsics translated to ARM NEON by the vendored `sse2neon` header.

## Demo and benchmark numbers

On macOS ARM64 with Clang and Make, open a terminal in the repository:

```sh
make
./bench_cluster mixed
```

Point to the line that publishes the strong `_gemm_compute` symbol. Read the fallback/SIMD call counts and the total of 200,000 verified tasks. The exact split and timing depend on thread scheduling.

The following ranges come from three runs per mode on macOS 15.5 ARM64 with Apple Clang 17. Each run used the same workload, and system load was uncontrolled. These figures describe the whole CPU pipeline.

| Mode | Verified tasks per run | Throughput | P50 | P99 |
|---|---:|---:|---:|---:|
| Fallback only | 200,000 | 526k–786k tasks/s | 0.219–0.298 ms | 0.637–0.784 ms |
| SIMD only | 200,000 | 837k–1.20M tasks/s | 0.140–0.196 ms | 0.421–0.624 ms |
| Live swap | 200,000 | 754k–1.11M tasks/s | 0.135–0.207 ms | 0.441–0.597 ms |

Throughput includes submission, computation and validation. P50 is the median task latency. P99 is the time within which roughly 99% of tasks finish. The per-task clock starts before enqueue and ends before output validation.

To show **separate processes** instead, start `./kubepatch_daemon` in one terminal, wait for readiness, then run `./run_k8s_pods.sh 60` in another. Stop the daemon after the clients finish. The script name is historical. All 16 clients are local processes.

## Likely questions

**Is this a Kubernetes or GPU project?**

“The implemented runtime runs CPU kernels on macOS ARM64. I can demonstrate local process communication through POSIX shared memory and a live kernel swap in the threaded benchmark.”

**Which part changes during a swap?**

“The function address in the symbol registry. The queued job format and calling convention stay the same. A worker that has already taken the old address can finish on the old mapping.”

**How do you know the results are right?**

“The clients and benchmark use deterministic inputs and compare each value with an independent expected result. There’s also a test for a non-square matrix with an inner dimension of seven, which exercises the vector tail. The daemon test covers GEMM, LayerNorm and ReLU.”

**Can I claim a SIMD speedup?**

“You can report the observed whole-runtime throughput ranges under this workload. The benchmark includes preparation and validation. An isolated kernel microbenchmark is a separate measurement.”

**Does the daemon also swap live?**

“The live swap is exercised by the threaded benchmark. The daemon loads its three operations at startup and dispatches verified jobs from local clients.”

**Is this ready for production tenants?**

“It’s a same-user demo intended for cooperating clients. It polls for work and doesn’t reclaim buffers abandoned by crashed clients. The loader takes trusted, self-contained objects.”

## Source map

- [Benchmark and live swap](benchmarks/bench_cluster.c)
- [Queue](src/core/ringbuffer.c) and [slot ownership and verification](src/core/workload.c)
- [Mach-O loader](src/linker/macho_loader.c), [executable mapping](src/linker/patcher.c) and [atomic registry](src/linker/symbol_table.c)
- [SIMD GEMM](kernels/gemm_opt.c) and [process integration test](tests/local_ipc.sh)
