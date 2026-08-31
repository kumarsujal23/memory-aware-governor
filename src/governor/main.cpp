#include "governor/governor_daemon.h"
#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

using namespace governor;

std::atomic<bool> g_stop_flag{false};

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        g_stop_flag = true;
    }
}

int main(int argc, char** argv) {
    DaemonConfig config;
    std::string log_level = "info";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--psi-path" && i + 1 < argc) {
            config.psi_path = argv[++i];
        } else if (arg == "--socket-path" && i + 1 < argc) {
            config.socket_path = argv[++i];
        } else if (arg == "--poll-interval" && i + 1 < argc) {
            config.poll_interval_ms = std::stoi(argv[++i]);
        } else if (arg == "--log-level" && i + 1 < argc) {
            log_level = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: governor_daemon [options]\n"
                      << "Options:\n"
                      << "  --psi-path <path>       (default: /proc/pressure/memory)\n"
                      << "  --socket-path <path>    (default: /tmp/mem_governor.sock)\n"
                      << "  --poll-interval <ms>    (default: 200)\n"
                      << "  --log-level <level>     (default: info)\n"
                      << "  --help                  Show this message\n";
            return 0;
        }
    }

    std::cout << "========================================\n"
              << " Memory-Pressure-Aware Cache Governor\n"
              << "========================================\n"
              << " PSI Path:       " << config.psi_path << "\n"
              << " Socket Path:    " << config.socket_path << "\n"
              << " Poll Interval:  " << config.poll_interval_ms << " ms\n"
              << " Log Level:      " << log_level << "\n"
              << "========================================\n";

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    GovernorDaemon daemon(config);
    
    try {
        daemon.start();
        while (!g_stop_flag) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "\n[Main] Shutdown signal received. Stopping...\n";
        daemon.stop();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
