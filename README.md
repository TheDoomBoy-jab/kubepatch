# KubePatch

An experimental C runtime combining a shared-memory task queue, an arena allocator and a Mach-O object loader for live kernel replacement on macOS/ARM64.

## Build and measure

```sh
make -B
./bench_cluster
```

The benchmark launches client threads and worker dispatchers in one process. Its label "pods" does not establish Kubernetes deployment or cross-node communication. The shared-memory daemon/client experiment requires processes that can access the same shared-memory object. This is not a distributed replacement for Kubernetes inference infrastructure.

The benchmark computes throughput and latency from the current run. Fixed results and comparisons against other serving systems have been removed because they are not supported by a controlled comparison. A local run during the portfolio review processed 200,000 tasks in 0.7954 seconds, reporting P50 2.471 ms and P99 17.227 ms. That run overlapped ML training and is not an isolated performance characterization. Task-count agreement does not independently prove numerical correctness, lock-free progress, or zero dropped work under every schedule.

The runtime and hot-patching mechanisms remain experimental. Use dedicated correctness and contention testing before making production claims.
