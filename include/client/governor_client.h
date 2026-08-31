#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include "governor/protocol.h"  // shared wire protocol

/**
 * GovernorClient — minimal client library for applications that want to be
 * governed by the memory-pressure-aware cache governor daemon.
 *
 * Connects to the daemon over a Unix domain socket, sends REGISTER /
 * REPORT_SIZE messages, and dispatches SHRINK / GROW commands received
 * from the daemon to user-supplied callbacks.
 *
 * Thread safety:
 *   - on_shrink/on_grow callbacks are dispatched from the listener thread.
 *   - send operations (register_cache, report_size) are mutex-protected.
 *   - start_listening/stop_listening must be called from the owning thread.
 */
class GovernorClient {
public:
    explicit GovernorClient(const std::string& socket_path = governor::SOCKET_PATH);
    ~GovernorClient();

    // Connection lifecycle
    bool connect();
    void disconnect();

    // Client → daemon messages
    bool register_cache(uint64_t current_size_bytes);
    bool report_size(uint64_t current_size_bytes);

    // Daemon → client callbacks
    void on_shrink(std::function<void(uint64_t target_bytes)> callback);
    void on_grow(std::function<void(uint64_t target_bytes)> callback);

    // Background listener thread
    void start_listening();
    void stop_listening();

    bool is_connected() const;

private:
    std::string socket_path_;
    int sock_fd_;
    std::atomic<bool> connected_;
    std::atomic<bool> listening_;

    std::thread listener_thread_;
    std::mutex send_mutex_;

    std::function<void(uint64_t)> shrink_callback_;
    std::function<void(uint64_t)> grow_callback_;

    void listener_loop();
    bool send_message(const governor::Message& msg);
    void process_message(const governor::Message& msg);
};
