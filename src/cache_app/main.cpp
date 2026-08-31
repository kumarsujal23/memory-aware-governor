/**
 * cache_app — Standalone workload driver for the GovernedCache.
 *
 * Generates a configurable Zipf-distributed key-access pattern (80 % gets,
 * 20 % puts) against a GovernedCache instance that is connected to the
 * governor daemon.  Reports per-interval and summary hit-rate statistics.
 *
 * Build:  linked against governed_cache + governor_client static libs.
 * Usage:  cache_app [--capacity N] [--duration N] [--zipf-skew 0.99] ...
 */
#include "client/governed_cache.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ─── Globals ────────────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};

static void signal_handler(int) { g_running = false; }

// ─── Zipfian Generator (rejection-inversion method) ─────────────────────────
//
// Generates integers in [1, n] following a Zipf distribution with exponent
// `alpha`.  Uses the rejection-inversion algorithm from:
//   W. Hörmann & G. Derflinger, "Rejection-inversion to generate variates
//   from monotone discrete distributions", 1996.
//
// This runs in O(1) per sample after O(1) precomputation, suitable for
// generating millions of keys per second in a benchmark loop.

class ZipfianGenerator {
public:
    ZipfianGenerator(uint64_t n, double alpha)
        : gen_(std::random_device{}())
    {
        std::vector<double> weights(n);
        for (uint64_t i = 0; i < n; ++i) {
            weights[i] = 1.0 / std::pow(static_cast<double>(i + 1), alpha);
        }
        dist_ = std::discrete_distribution<uint64_t>(weights.begin(), weights.end());
    }

    uint64_t next() {
        return dist_(gen_) + 1;
    }

private:
    std::mt19937_64 gen_;
    std::discrete_distribution<uint64_t> dist_;
};

// ─── Helpers ────────────────────────────────────────────────────────────────

static std::string random_string(size_t length) {
    static constexpr char kChars[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> pick(0, sizeof(kChars) - 2);

    std::string s(length, '\0');
    for (size_t i = 0; i < length; ++i)
        s[i] = kChars[pick(rng)];
    return s;
}

// ─── Configuration ──────────────────────────────────────────────────────────

struct Config {
    size_t   capacity        = 100000;
    size_t   entry_size      = 256;
    double   zipf_skew       = 0.99;
    uint64_t key_space       = 500000;
    uint64_t ops_per_sec     = 10000;   // 0 = unlimited
    uint64_t duration_sec    = 60;
    std::string socket_path  = "/tmp/mem_governor.sock";
    uint64_t report_interval = 1;       // seconds
};

static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Missing value for " << arg << "\n";
            std::exit(1);
        };
        if      (arg == "--capacity")        cfg.capacity        = std::stoull(next());
        else if (arg == "--entry-size")      cfg.entry_size      = std::stoull(next());
        else if (arg == "--zipf-skew")       cfg.zipf_skew       = std::stod(next());
        else if (arg == "--key-space")       cfg.key_space       = std::stoull(next());
        else if (arg == "--ops-per-sec")     cfg.ops_per_sec     = std::stoull(next());
        else if (arg == "--duration")        cfg.duration_sec    = std::stoull(next());
        else if (arg == "--socket-path")     cfg.socket_path     = next();
        else if (arg == "--report-interval") cfg.report_interval = std::stoull(next());
        else if (arg == "--help") {
            std::cout
                << "Usage: cache_app [options]\n"
                << "  --capacity N          Max items in cache       (default: 100000)\n"
                << "  --entry-size BYTES    Value size per entry     (default: 256)\n"
                << "  --zipf-skew FLOAT     Zipf exponent s in (0,1)(default: 0.99)\n"
                << "  --key-space N         Total unique keys        (default: 500000)\n"
                << "  --ops-per-sec N       Target ops/s, 0=max     (default: 10000)\n"
                << "  --duration SECS       Run duration             (default: 60)\n"
                << "  --socket-path PATH    Governor socket          (default: /tmp/mem_governor.sock)\n"
                << "  --report-interval SEC Stats print interval     (default: 1)\n";
            std::exit(0);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::exit(1);
        }
    }
    return cfg;
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    Config cfg = parse_args(argc, argv);

    std::cout
        << "╔══════════════════════════════════════════════╗\n"
        << "║       Governed Cache — Workload Driver       ║\n"
        << "╠══════════════════════════════════════════════╣\n"
        << "║  Capacity:       " << std::setw(10) << cfg.capacity        << "           ║\n"
        << "║  Entry size:     " << std::setw(10) << cfg.entry_size      << " B         ║\n"
        << "║  Key space:      " << std::setw(10) << cfg.key_space       << "           ║\n"
        << "║  Zipf skew:      " << std::setw(10) << std::fixed << std::setprecision(2) << cfg.zipf_skew << "           ║\n"
        << "║  Ops/sec target: " << std::setw(10)
        << (cfg.ops_per_sec == 0 ? "MAX" : std::to_string(cfg.ops_per_sec)) << "           ║\n"
        << "║  Duration:       " << std::setw(10) << cfg.duration_sec    << " s         ║\n"
        << "╚══════════════════════════════════════════════╝\n\n";

    // Create governed cache and connect to daemon
    GovernedCache cache(cfg.capacity, cfg.socket_path, cfg.entry_size);
    cache.start();

    // Zipf key generator — exponent is (skew + 1.0) because the standard
    // Zipf distribution uses α > 1 and the user specifies the "skew" s ∈ (0,1).
    ZipfianGenerator zipf(cfg.key_space, cfg.zipf_skew + 1.0);
    std::mt19937 op_rng(std::random_device{}());
    std::uniform_real_distribution<double> op_coin(0.0, 1.0);

    // Pre-generate a reusable random value template
    std::string value_template = random_string(cfg.entry_size);

    auto start_time  = std::chrono::steady_clock::now();
    auto last_report = start_time;
    auto end_time    = start_time + std::chrono::seconds(cfg.duration_sec);

    uint64_t ops_since_report = 0;
    uint64_t total_ops        = 0;

    // Throttle: inter-operation delay (0 if unlimited)
    const auto delay_per_op = (cfg.ops_per_sec > 0)
        ? std::chrono::nanoseconds(1'000'000'000ULL / cfg.ops_per_sec)
        : std::chrono::nanoseconds(0);

    // ── Main workload loop ──────────────────────────────────────────────────
    while (g_running && std::chrono::steady_clock::now() < end_time) {
        auto op_start = std::chrono::steady_clock::now();

        uint64_t key_id = zipf.next();
        std::string key = "key_" + std::to_string(key_id);

        if (op_coin(op_rng) < 0.8) {
            cache.get(key);           // 80 % reads
        } else {
            cache.put(key, value_template);  // 20 % writes
        }

        ++ops_since_report;
        ++total_ops;

        // ── Periodic stats report ───────────────────────────────────────────
        auto now = std::chrono::steady_clock::now();
        auto since_report = std::chrono::duration_cast<std::chrono::seconds>(now - last_report).count();
        if (static_cast<uint64_t>(since_report) >= cfg.report_interval) {
            auto stats = cache.get_stats();
            double elapsed_sec = std::chrono::duration<double>(now - last_report).count();
            auto   current_ops = static_cast<uint64_t>(ops_since_report / elapsed_sec);

            std::time_t t = std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now());
            char ts[32];
            std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&t));

            std::cout << "[" << ts << "] "
                      << "cache_size=" << stats.size
                      << " capacity="  << stats.capacity
                      << " hit_rate="  << std::fixed << std::setprecision(2)
                                       << (stats.hit_rate * 100.0) << "%"
                      << " ops="       << current_ops
                      << "\n";

            ops_since_report = 0;
            last_report      = now;
        }

        // ── Throttle ────────────────────────────────────────────────────────
        if (delay_per_op.count() > 0) {
            auto op_dur = std::chrono::steady_clock::now() - op_start;
            if (op_dur < delay_per_op)
                std::this_thread::sleep_for(delay_per_op - op_dur);
        }
    }

    cache.stop();

    // ── Summary ─────────────────────────────────────────────────────────────
    auto final_stats = cache.get_stats();
    std::cout
        << "\n── Run Summary ────────────────────────────\n"
        << "Total Ops:        " << total_ops << "\n"
        << "Final Hit Rate:   " << std::fixed << std::setprecision(2)
                                << (final_stats.hit_rate * 100.0) << "%\n"
        << "Final Cache Size: " << final_stats.size << "\n"
        << "Final Capacity:   " << final_stats.capacity << "\n";

    return 0;
}
