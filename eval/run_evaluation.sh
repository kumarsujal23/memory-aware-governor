#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# Evaluation script for the Memory-Pressure-Aware Cache Governor
#
# Runs three conditions with identical workloads and pressure profiles,
# then reports the core trade-off metrics:
#   1) OOM kill count
#   2) Cumulative PSI stall time (µs)
#   3) Cache hit rate
#
# Usage:
#   ./eval/run_evaluation.sh [--duration 60] [--pressure-mb 512] [--cache-capacity 100000]
# ═══════════════════════════════════════════════════════════════════════════════
set -euo pipefail

# ── Default parameters ───────────────────────────────────────────────────────
DURATION=60
PRESSURE_MB=512
CACHE_CAPACITY=100000
BUILD_DIR="./build"
CONDITION="all"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --duration)       DURATION="$2";       shift 2 ;;
        --pressure-mb)    PRESSURE_MB="$2";    shift 2 ;;
        --cache-capacity) CACHE_CAPACITY="$2"; shift 2 ;;
        --build-dir)      BUILD_DIR="$2";      shift 2 ;;
        --condition)      CONDITION="$2";      shift 2 ;;
        --help)
            echo "Usage: $0 [--duration N] [--pressure-mb N] [--cache-capacity N] [--build-dir path] [--condition no_governor|reactive_only|full_governor|all]"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Verify binaries exist
for bin in governor_daemon cache_app pressure_gen; do
    if [[ ! -x "${BUILD_DIR}/${bin}" ]]; then
        echo "ERROR: ${BUILD_DIR}/${bin} not found. Did you build the project?"
        echo "  mkdir -p build && cd build && cmake .. && make -j\$(nproc)"
        exit 1
    fi
done

RESULTS_DIR="eval/results"
mkdir -p "${RESULTS_DIR}"

# ── Helper: get appropriate PSI path ─────────────────────────────────────────
get_psi_path() {
    if [[ -f "/sys/fs/cgroup/memory.pressure" ]]; then
        echo "/sys/fs/cgroup/memory.pressure"
    else
        echo "/proc/pressure/memory"
    fi
}
PSI_FILE=$(get_psi_path)

# ── Helper: read PSI total stall time (µs) ───────────────────────────────────
read_psi_total() {
    set +o pipefail
    local val
    val=$(awk '/^some/ { for (i=1; i<=NF; i++) if ($i ~ /^total=/) print substr($i,7) }' "$PSI_FILE" 2>/dev/null)
    if [[ -z "$val" ]]; then val=0; fi
    echo "$val"
    set -o pipefail
}

# ── Helper: cleanup child processes ──────────────────────────────────────────
cleanup() {
    local pids=("$@")
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    wait 2>/dev/null || true
}

# ── Run a single evaluation condition ────────────────────────────────────────
#  $1  = condition name (no_governor | reactive_only | full_governor)
#  $2+ = extra governor_daemon args (if any)
run_condition() {
    local cond="$1"; shift
    local gov_args=("$@")
    local pids_to_clean=()

    echo ""
    echo "══════════════════════════════════════════════"
    echo " Condition: ${cond}"
    echo "══════════════════════════════════════════════"

    # Snapshot initial PSI total and initial OOM kills (if using cgroups)
    local initial_psi
    initial_psi=$(read_psi_total)
    local initial_ooms=0
    if [[ -f "/sys/fs/cgroup/memory.events" ]]; then
        initial_ooms=$(awk '/oom_kill / {print $2}' /sys/fs/cgroup/memory.events 2>/dev/null || echo 0)
        if [[ -z "$initial_ooms" ]]; then initial_ooms=0; fi
    fi

    # Clear kernel ring buffer (needs root, best-effort)
    dmesg -C 2>/dev/null || true

    # 1. Start governor daemon (unless no_governor)
    local GOV_PID=""
    if [[ "$cond" != "no_governor" ]]; then
        "${BUILD_DIR}/governor_daemon" "${gov_args[@]}" \
            > "${RESULTS_DIR}/${cond}_governor.log" 2>&1 &
        GOV_PID=$!
        pids_to_clean+=("$GOV_PID")
        echo "  [+] Governor daemon started (PID ${GOV_PID})"
        sleep 1  # let socket bind
    fi

    # 2. Start cache workload application
    "${BUILD_DIR}/cache_app" \
        --capacity "${CACHE_CAPACITY}" \
        --entry-size 16384 \
        --zipf-skew 0.5 \
        --key-space "${CACHE_CAPACITY}" \
        --ops-per-sec 0 \
        --duration $(( DURATION + 20 )) \
        > "${RESULTS_DIR}/${cond}_cache.log" 2>&1 &
    local CACHE_PID=$!
    pids_to_clean+=("$CACHE_PID")
    echo "  [+] Cache app started (PID ${CACHE_PID})"

    # 3. Warmup period
    echo "  [~] Warming up cache (5s) ..."
    sleep 5

    # 4. Inject memory pressure
    echo "  [~] Injecting pressure: ${PRESSURE_MB} MB over ${DURATION}s (ramp mode) ..."
    "${BUILD_DIR}/pressure_gen" \
        --mode ramp \
        --max-mb "${PRESSURE_MB}" \
        --duration "${DURATION}" \
        > "${RESULTS_DIR}/${cond}_pressure.log" 2>&1 &
    local PRES_PID=$!
    pids_to_clean+=("$PRES_PID")
    wait "$PRES_PID" 2>/dev/null || true

    # 5. Recovery period
    echo "  [~] Recovery period (10s) ..."
    sleep 10

    # 6. Snapshot final PSI total
    local final_psi
    final_psi=$(read_psi_total)
    local psi_diff=$(( final_psi - initial_psi ))

    # 7. Collect OOM kills (prefer cgroup v2 memory.events, fallback to dmesg)
    set +o pipefail
    local oom_kills=0
    if [[ -f "/sys/fs/cgroup/memory.events" ]]; then
        local final_ooms
        final_ooms=$(awk '/oom_kill / {print $2}' /sys/fs/cgroup/memory.events 2>/dev/null || echo 0)
        if [[ -z "$final_ooms" ]]; then final_ooms=0; fi
        oom_kills=$(( final_ooms - initial_ooms ))
    else
        oom_kills=$(dmesg 2>/dev/null | grep -i "Killed process" | wc -l)
        if [[ -z "$oom_kills" ]]; then oom_kills=0; fi
    fi

    # 8. Collect cache hit rate from app log
    local hit_rate
    hit_rate=$(awk -F 'Hit Rate:[ \t]*' '/Hit Rate:/ {sub("%", "", $2); print $2}' "${RESULTS_DIR}/${cond}_cache.log" | tail -1)
    if [[ -z "$hit_rate" ]]; then
        hit_rate=$(awk -F 'hit_rate=' '/hit_rate=/ {split($2, a, "%"); print a[1]}' "${RESULTS_DIR}/${cond}_cache.log" | tail -1)
    fi
    if [[ -z "$hit_rate" ]]; then hit_rate="0.0"; fi
    set -o pipefail

    # 9. Cleanup
    cleanup "${pids_to_clean[@]}"

    # 10. Record result
    echo "${cond},${oom_kills},${psi_diff},${hit_rate}" >> "${RESULTS_DIR}/summary.csv"
    echo "  [✓] OOM kills: ${oom_kills} | PSI stall: ${psi_diff} µs | Hit rate: ${hit_rate}%"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main evaluation
# ═══════════════════════════════════════════════════════════════════════════════
echo "Memory-Pressure-Aware Cache Governor — Evaluation"
echo "Duration: ${DURATION}s  Pressure: ${PRESSURE_MB} MB  Cache: ${CACHE_CAPACITY} entries"
echo ""

# CSV header (append mode when running single conditions)
if [[ "$CONDITION" == "all" ]] || [[ ! -f "${RESULTS_DIR}/summary.csv" ]]; then
    echo "Condition,OOM_Kills,PSI_Stall_Total_us,Hit_Rate_pct" > "${RESULTS_DIR}/summary.csv"
fi

# Run conditions based on --condition flag
if [[ "$CONDITION" == "all" || "$CONDITION" == "no_governor" ]]; then
    run_condition "no_governor"
fi

if [[ "$CONDITION" == "all" || "$CONDITION" == "reactive_only" ]]; then
    run_condition "reactive_only" \
        --psi-path "$PSI_FILE"
fi

if [[ "$CONDITION" == "all" || "$CONDITION" == "full_governor" ]]; then
    run_condition "full_governor" \
        --psi-path "$PSI_FILE"
fi

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════"
echo "                   EVALUATION RESULTS"
echo "═══════════════════════════════════════════════════════"
column -t -s',' "${RESULTS_DIR}/summary.csv"
echo ""
echo "Detailed logs saved to: ${RESULTS_DIR}/"
