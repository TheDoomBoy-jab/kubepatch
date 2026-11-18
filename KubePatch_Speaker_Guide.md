# KubePatch speaker guide

## Opening

“KubePatch is a C runtime for small matrix jobs on the CPU. It keeps each job’s data in a reusable memory slot and puts the job description on a queue. During the benchmark, workers start with a fallback matrix multiply. The runtime then loads a SIMD version and updates the function address that workers look up, while the process keeps running.”

A **kernel** is a numerical routine in this project. **GEMM** means general matrix multiplication. **SIMD** means one instruction operates on multiple data lanes.

## Rehearsal plan

Allow about **17 minutes** with a short demo. Use the captured terminal output on slide 32 if you are presenting away from an Apple silicon Mac.

| Slides | Topic | Allow |
|---|---|---:|
| 1–5 | Purpose, stack and one job from start to finish | 2 min |
| 6–13 | Process models, descriptors, queue and buffer ownership | 3 min |
| 14–24 | CPU kernels, output checks, object loading and the swap | 5 min |
| 25–30 | Local daemon and benchmark results | 3 min |
| 31–36 | Run the demos, cover scope and close | 4 min |

## Slide-by-slide talk track

### 1. KubePatch

Open with the project in one sentence. KubePatch is a local CPU runtime that queues small numerical jobs and can switch the matrix-multiply implementation while workers keep running. The benchmark and the daemon demonstrate different parts of that design.

### 2. The cost of handing off a job

Explain that the project explores overhead for frequent local work. The descriptor carries metadata, so the handoff does not need to serialize matrix bytes. The repo does not provide a socket baseline for quantifying savings from this mechanism.

### 3. Why change a kernel live?

A kernel here is a numerical routine. The task format stays fixed during the change. The threaded benchmark starts with a fallback and publishes the SIMD implementation while workers are already active.

### 4. Tech stack

C implements the runtime and kernels. Clang produces object files. Mach-O is the object format the loader reads. ARM NEON is a CPU instruction set extension for vector operations.

### 5. A job from submission to result

Walk through one task before discussing internals. The benchmark validates the result on the producer side after the worker publishes completion. In the daemon path, the client performs that check.

### 6. Two demos, two execution paths

The benchmark is called bench_cluster for historical reasons. It is a one-process test. The local launcher starts separate processes on the same Mac and exercises the shared-memory path.

### 7. What a task descriptor carries

Show the task as a compact contract between producer and consumer. The descriptor has other reserved metadata fields. The implemented dispatcher uses ID, buffer offsets and dimensions. The raw matrix data stays outside the queue.

### 8. Why offsets travel across processes

POSIX shared memory maps the same backing storage into each process. Virtual addresses can differ. An offset remains meaningful when the two mappings use the same layout.

### 9. The bounded ring buffer

Define MPMC as multiple producers, multiple consumers. The fixed allocation gives the ring a known capacity. The slot index uses a bit mask because the size is a power of two. The separate arena supports 256 active buffers, which also bounds the number of in-flight jobs in this workload.

### 10. How a queue slot gets reused

The compare-and-swap chooses an owner. Release and acquire on the slot sequence publish the payload to the consumer. After dequeue the sequence advances by one ring capacity, identifying the next generation of the same slot.

### 11. What happens while a thread waits

This is a polling design. ARM yield is used as a hint in the spin loop. Waiting avoids allocating more buffers, but it can consume CPU time. The daemon client has timeout and readiness checks.

### 12. The buffer arena

The benchmark assigns every producer a private window of 32 slots. A local client reserves a free slot atomically from the shared arena. The word slot appears in both queue and arena discussions. Queue slots store descriptors, while arena slots store actual input and output buffers.

### 13. Who owns a buffer?

Ownership matters more than the allocation algorithm. The worker must finish writing before the client or producer reads. The client publishes a free state only after verification. The benchmark with dedicated windows reuses slots only after checking all previous work on each slot.

### 14. The three CPU kernels

The separate local daemon rotates between these operations through the client. The GEMM benchmark is narrower: it exercises two versions of the matrix multiply on a fixed shape.

### 15. The GEMM workload

Describe matrix multiplication as many dot products. Each benchmark job has M, N and K all equal to 16. The loaded functions accept dimensions, and a direct test exercises an additional non-square shape. The shared task dispatcher uses the fixed demo shape.

### 16. Scalar vs. SIMD multiplication

Clarify that the fallback is defined by scalar source. Do not infer its exact instruction sequence from the source alone. The optimized kernel explicitly expresses four float lanes through SSE-style intrinsics translated to NEON.

### 17. Inside the SIMD loop

B is accessed by column, so those reads are strided. The loop ends with a lane reduction, followed by the scalar tail for K values that are not divisible by four. These costs explain why vector width alone does not predict a speedup.

### 18. How results are checked

The prepared output begins as NaN, so an unwritten output cannot pass the exact comparison. The direct test checks both loaded GEMM versions on a different shape. The local client also verifies expected LayerNorm and ReLU results.

### 19. The two GEMM object files

Clang compiles both objects before the program runs. Their signatures match, including input pointers, output pointer, and the three dimensions. Weak and strong refer to symbol binding and determine which address wins registration.

### 20. What the Mach-O loader reads

The loader handles a deliberately small subset for trusted, self-contained compiled code. It checks ranges before reading object fields and rejects text relocations. General linking and external symbol resolution are outside its scope.

### 21. From object bytes to a callable function

RW means read/write and RX means read/execute. The runtime does not leave the mapping both writable and executable. After the cache operation, the runtime can call the function at the mapped base plus its symbol offset.

### 22. Which symbol wins?

An incoming weak definition cannot replace an existing strong entry. A duplicate strong registration is rejected. The runtime test exercises all three cases and confirms which pointer is selected.

### 23. Making a live swap safe

An atomic pointer update and executable code lifetime solve different problems. Later lookups can select SIMD. Earlier calls can still execute fallback. Both mappings are freed only after worker threads have been joined.

### 24. A live swap during the benchmark

One thousand is the minimum before the main thread attempts the switch. It is not a promise about the exact split: workers may finish more fallback calls while the strong image loads. The run fails if either variant is unused in mixed mode.

### 25. The local daemon path

This is the separate POSIX shared-memory path. The daemon loads all three implementations at startup and keeps their addresses for dispatch. Clients submit one job at a time, cycling between kernels.

### 26. The benchmark workload

Each job performs a 16 by 16 matrix multiply and prepares deterministic data. Identical producer counts, worker counts and shapes are used in the three benchmark modes. The quoted workload is local CPU work.

### 27. Three ways to run the benchmark

Use the single-kernel modes to compare whole-runtime throughput. Use mixed mode to show replacement and see how many jobs went to each implementation. The three modes all perform numerical output checks.

### 28. What the timers include

Latency includes waiting to enqueue, time in the ring, worker scheduling and execution. P50 is the median. P99 describes the slow end of the distribution. Throughput and latency answer different questions.

### 29. Measured throughput

Three runs per mode on macOS 15.5 ARM64 with Apple Clang 17. The system load was not controlled. SIMD-only was faster across these sample ranges. The numbers include job preparation, queueing, dispatch and numerical verification, so they describe whole-runtime throughput for this fixed workload.

### 30. Reading the results

Be precise about what you measured. The benchmark includes scheduling, initialization, and result checks. The machine is an Apple silicon Mac and system load was uncontrolled. The live-swap mode mixes kernels, so its range does not isolate the cost of loading or switching.

### 31. Demo 1 · Watch a kernel change

Run this from the repo root on macOS ARM64. The objects must exist at their expected relative paths. The printed split and timings vary. The 200,000 verified count and the presence of both versions are the key success signals.

### 32. What successful output looks like

Explain this captured example. The fallback and SIMD counts sum to 200,000. The code checks all numerical outputs before reporting success. The next run can have a different split and different latency.

### 33. Demo 2 · Use separate processes

Wait for the daemon readiness message before running the launcher in the second terminal. The shell script name is historical. It starts local binaries. Stop the daemon after the clients exit. The integration test automates this exact 960-job path.

### 34. What the test suite covers

Use make test to run the unit-style checks, all three benchmark modes and the separate daemon/client integration. Each benchmark mode verifies 200,000 matrix results. The client test verifies 960 jobs and checks that a client cannot connect after shutdown.

### 35. Current scope and next measurements

The repo supports local processes and a threaded benchmark. Kubernetes, GPU execution and remote networking are outside the implemented runtime. The loader expects trusted, relocation-free text. For a broader runtime, add resource recovery and controlled measurements before making claims about other platforms.

### 36. The takeaway

Close by tying the two paths together. The buffer and queue provide a stable submission interface. The benchmark shows a live replacement and checks all results. The separate process demo verifies the shared-memory transport. Invite questions on the queue, loader or benchmark.

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
