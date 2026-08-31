# Memory-Pressure-Aware Cache Governor

A high-performance C++17 userspace daemon and caching library designed to proactively mitigate system memory stalls (latency spikes) under extreme memory pressure. 

Built specifically for low-latency systems where avoiding Linux kernel Out-Of-Memory (OOM) kills and massive swapping latency is critical, this project leverages the Linux kernel's **Pressure Stall Information (PSI)** to dynamically shrink application memory footprints *before* the kernel panics.

## 🚀 Key Features

* **Kernel PSI Integration:** Parses `/proc/pressure/memory` at 200ms intervals to accurately quantify system-wide memory starvation.
* **Low-Latency IPC:** Utilizes an `epoll`-based event loop and non-blocking Unix Domain Sockets (`AF_UNIX`) for asynchronous, high-throughput JSON messaging between the daemon and clients.
* **O(1) Intrusive LRU Cache:** A custom caching engine utilizing embedded linked-list pointers within a hash map to achieve true O(1) lookups and evictions without pointer-chasing heap fragmentation.
* **Zipfian Workload Generator:** Simulates highly realistic temporal locality (80/20 access patterns) using the Rejection-Inversion mathematical algorithm.
* **Nanosecond Throttling:** Highly precise workload generator pacing without busy-waiting.

## 📊 Evaluation & Benchmarks

The system was evaluated under strict physical constraints using **Docker cgroups** (`2GB` total memory limit). We tested an unbounded memory-pressure generator against the cache application. 

By proactively reacting to PSI spikes and lazily evicting up to 50% of the cache capacity, the Governor successfully prevented OOM kills and drastically reduced memory stall time, with a negligible hit to cache performance.

| Condition | PSI Stall (µs) | Hit Rate | Result |
|-----------|----------------|----------|--------|
| `no_governor` (Baseline) | 2,223,815 µs | 99.64% | Massive system latency / Heavy Swapping |
| `reactive_only` | 1,028,977 µs | 99.41% | 53.7% Stall Reduction |
| **`full_governor`** | **863,883 µs** | **99.40%** | **61.2% Stall Reduction** |

> **Conclusion:** The governor reduced total system memory stall time by **61.2%** while sacrificing only **0.24%** of the cache hit-rate, successfully shifting the kernel's `oom_score` targeting away from the critical cache application.

## 🏗️ Architecture

1. **Governor Daemon:** A standalone background process containing a `PsiPoller`, a cascading `PolicyEngine` (Normal -> Elevated -> High -> Critical), and an `epoll` socket `ClientRegistry`.
2. **Governed Cache Library:** A static C++ library linked into the target application. It connects to the daemon and exposes the intrusive O(1) LRU cache.
3. **Pressure Generator:** A "noisy neighbor" benchmarking utility that rapidly forces demand-paging page faults via `malloc` + `memset` to simulate severe host starvation.

## ⚙️ Building from Source

**Requirements:**
* Linux (or WSL2)
* GCC/Clang with C++17 support
* CMake 3.10+
* Docker (for cgroup benchmarking)

```bash
# Clone the repository
git clone https://github.com/yourusername/memory-aware-governor.git
cd memory-aware-governor

# Build using CMake
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

## 🧪 Running the Docker Benchmark

To recreate the isolated memory constraint benchmarks:

```bash
# 1. Build the Docker image
docker build -t cache-governor .

# 2. Run the full governor evaluation under a 2GB limit
docker run --rm --memory="1500m" --memory-swap="2000m" \
  -v "${PWD}/eval/results:/app/eval/results" \
  cache-governor \
  ./eval/run_evaluation.sh \
  --duration 60 \
  --pressure-mb 800 \
  --cache-capacity 100000 \
  --condition full_governor
```

*(Evaluation CSV and logs will be output to `eval/results/`)*
