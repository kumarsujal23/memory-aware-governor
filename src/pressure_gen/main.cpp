#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <atomic>

using namespace std;

atomic<bool> keep_running{true};

void sigint_handler(int) {
    keep_running = false;
}

void print_help() {
    cout << "Usage: pressure_gen [options]\n"
         << "Options:\n"
         << "  --mode <mode>        ramp | spike | wave | hold (default: ramp)\n"
         << "  --max-mb <MB>        Maximum memory to allocate in MB (default: 512)\n"
         << "  --duration <sec>     Total duration in seconds (default: 30)\n"
         << "  --ramp-time <sec>    Time to ramp up in seconds (default: 10)\n"
         << "  --hold-time <sec>    Time to hold at max in seconds (default: 10)\n"
         << "  --release-time <sec> Time to ramp down in seconds (default: 10)\n"
         << "  --wave-period <sec>  Period of wave in seconds (default: 20)\n"
         << "  --touch-pages <bool> Whether to touch allocated pages true|false (default: true)\n"
         << "  --help               Show this help message\n";
}

int main(int argc, char* argv[]) {
    string mode = "ramp";
    size_t max_mb = 512;
    int duration = 30;
    int ramp_time = 10;
    int hold_time = 10;
    int release_time = 10;
    int wave_period = 20;
    bool touch_pages = true;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (arg == "--max-mb" && i + 1 < argc) max_mb = stoull(argv[++i]);
        else if (arg == "--duration" && i + 1 < argc) duration = stoi(argv[++i]);
        else if (arg == "--ramp-time" && i + 1 < argc) ramp_time = stoi(argv[++i]);
        else if (arg == "--hold-time" && i + 1 < argc) hold_time = stoi(argv[++i]);
        else if (arg == "--release-time" && i + 1 < argc) release_time = stoi(argv[++i]);
        else if (arg == "--wave-period" && i + 1 < argc) wave_period = stoi(argv[++i]);
        else if (arg == "--touch-pages" && i + 1 < argc) touch_pages = (string(argv[++i]) == "true");
        else if (arg == "--help") {
            print_help();
            return 0;
        } else {
            cerr << "Unknown argument: " << arg << "\n";
            print_help();
            return 1;
        }
    }

    signal(SIGINT, sigint_handler);

    const size_t chunk_size = 1024 * 1024; // 1 MB chunks
    vector<void*> allocations;
    size_t current_mb = 0;

    auto allocate_to = [&](size_t target_mb) {
        while (current_mb < target_mb && keep_running) {
            void* ptr = malloc(chunk_size);
            if (!ptr) {
                cerr << "OOM at " << current_mb << " MB\n";
                break;
            }
            if (touch_pages) {
                memset(ptr, 0xAA, chunk_size);
            }
            allocations.push_back(ptr);
            current_mb++;
        }
        while (current_mb > target_mb && !allocations.empty()) {
            void* ptr = allocations.back();
            allocations.pop_back();
            free(ptr);
            current_mb--;
        }
    };

    auto start_time = chrono::steady_clock::now();
    cout << "Starting memory pressure generator in " << mode << " mode...\n";

    while (keep_running) {
        auto now = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(now - start_time).count();
        if (elapsed >= duration) break;

        size_t target_mb = 0;
        if (mode == "ramp") {
            if (elapsed < ramp_time) {
                target_mb = (elapsed / ramp_time) * max_mb;
            } else if (elapsed < ramp_time + hold_time) {
                target_mb = max_mb;
            } else if (elapsed < ramp_time + hold_time + release_time) {
                double release_elapsed = elapsed - ramp_time - hold_time;
                target_mb = max_mb - (release_elapsed / release_time) * max_mb;
            } else {
                target_mb = 0;
            }
        } else if (mode == "spike") {
            if (elapsed < hold_time) {
                target_mb = max_mb;
            } else {
                target_mb = 0;
            }
        } else if (mode == "wave") {
            double phase = (elapsed / wave_period) * 2.0 * M_PI;
            target_mb = (sin(phase - M_PI/2) + 1.0) / 2.0 * max_mb;
        } else if (mode == "hold") {
            target_mb = max_mb;
        }

        allocate_to(target_mb);
        cout << "Current allocation: " << current_mb << " MB\n";
        this_thread::sleep_for(chrono::seconds(1));
    }

    cout << "Cleaning up memory...\n";
    allocate_to(0);
    cout << "Done.\n";
    return 0;
}
