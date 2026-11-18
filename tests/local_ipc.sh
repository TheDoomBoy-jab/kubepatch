#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
export KUBEPATCH_SHM="/kp_test_$$"
log=$(mktemp)
daemon=
cleanup() {
    if [[ -n "$daemon" ]]; then
        kill -TERM "$daemon" 2>/dev/null || true
        wait "$daemon" || true
    fi
    rm -f "$log"
}
trap cleanup EXIT

./kubepatch_daemon >"$log" 2>&1 &
daemon=$!
for ((attempt=0; attempt<100; ++attempt)); do
    if grep -q 'daemon ready' "$log"; then
        break
    fi
    if ! kill -0 "$daemon" 2>/dev/null; then
        cat "$log"
        exit 1
    fi
    sleep 0.05
done
if ! grep -q 'daemon ready' "$log"; then
    cat "$log"
    exit 1
fi

./run_k8s_pods.sh 60
kill -TERM "$daemon"
wait "$daemon"
daemon=
cat "$log"
grep -q 'Executed 960 tasks, rejected 0' "$log"
if ./pod_client 1 1 >/dev/null 2>&1; then
    echo 'Client connected after daemon shutdown' >&2
    exit 1
fi
