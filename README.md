# KubePatch

KubePatch is a C runtime for small CPU compute jobs on Apple silicon. Producers pass task descriptors through an atomic ring buffer while matrix data stays in a reusable memory arena. The benchmark loads two Mach-O implementations of matrix multiplication and switches workers from the fallback to the SIMD implementation while jobs are running.

[Presentation](KubePatch_Presentation.pptx) · [Speaker guide](KubePatch_Speaker_Guide.md)

## Build and test

Use an Apple silicon Mac with the Xcode Command Line Tools. Run the commands from the repository root so the programs can find the kernel objects.

```sh
make
make test
```

The suite checks queue wraparound and FIFO order, symbol precedence, malformed-object rejection, a non-square GEMM with a SIMD tail, and invalid task handling. It also verifies every matrix in each benchmark mode and runs a 16-process shared-memory test covering GEMM, LayerNorm and ReLU.

## Show the live swap

```sh
./bench_cluster mixed
```

Eight producer threads submit 200,000 fixed-size 16 × 16 matrix jobs to two workers. The main thread waits for completed fallback calls, loads the strong SIMD symbol and publishes its address. Workers resolve the current address before each call. Both images stay mapped until workers exit. The benchmark reports the number of calls to each kernel and fails if a result differs from the reference.

Use `./bench_cluster fallback` and `./bench_cluster simd` for separate whole-runtime comparisons. Throughput includes preparing and verifying jobs. Per-task latency starts just before enqueue and ends after dispatch, before the producer verifies the output. These are one-host CPU measurements.

## Run the local daemon and clients

In one terminal:

```sh
./kubepatch_daemon
```

When it reports readiness, run in another terminal:

```sh
./pod_client 1 60
./run_k8s_pods.sh 60
```

The launcher starts 16 local client processes. Each client cycles through the three kernels and verifies the result of each request. Stop the daemon with Ctrl+C after the clients exit. Set the same `KUBEPATCH_SHM` value in both terminals to use a different shared-memory object name. A daemon killed without cleanup can leave its name behind. Use a fresh name for a subsequent local run.

## Design and scope

- The 1,024-entry MPMC ring uses C11 atomics and per-slot sequence counters. The daemon and clients map a named POSIX shared-memory object. The benchmark shares process memory among threads.
- The arena contains 256 reusable slots. A producer owns a slot through completion and validation, so workers retain valid input and output buffers.
- The loader accepts trusted, self-contained ARM64 Mach-O objects with no text relocations. It finds an entry point, copies text into writable pages, changes them to read/execute and invalidates the instruction cache. General relocation and external-symbol resolution are outside its scope.
- Writers serialize registry changes. Workers read published function addresses atomically. The benchmark exercises a live upgrade. The daemon loads its three kernels at startup.
- The daemon is a local, same-user demo for cooperating clients. It polls for work, expects clients to finish before daemon shutdown and does not reclaim slots abandoned by crashed clients. Kubernetes scheduling, remote clients and GPU execution are outside this runtime.
