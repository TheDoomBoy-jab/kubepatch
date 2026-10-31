#!/bin/bash
NUM_PODS=16
TASKS_PER_POD=10000

echo "=== Launching $NUM_PODS Concurrent Pod Processes ==="

pids=()
for ((i=1; i<=NUM_PODS; i++)); do
    ./pod_client $i $TASKS_PER_POD &
    pids+=($!)
done

# Wait for all 16 pods to complete
for pid in "${pids[@]}"; do
    wait $pid
done

echo "=== All $NUM_PODS Pods Finished Submitting Tasks! ==="