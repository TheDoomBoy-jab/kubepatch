#!/bin/bash
# Compatibility entry point for the local process demo.
# Start kubepatch_daemon separately before running this script.
set -u
cd "$(dirname "$0")" || exit 1
count=${1:-1000}
pids=()
for ((i=1; i<=16; ++i)); do
    ./pod_client "$i" "$count" &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    wait "$pid" || status=1
done
exit "$status"
