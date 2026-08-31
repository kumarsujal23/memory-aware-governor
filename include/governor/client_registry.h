#pragma once

#include "governor/protocol.h"
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace governor {

struct ClientInfo {
    int fd;
    pid_t pid;
    uint64_t current_size_bytes;
    uint64_t min_size_bytes;
    std::chrono::steady_clock::time_point connected_at;
};

class ClientRegistry {
public:
    ClientRegistry(std::string socket_path = SOCKET_PATH);
    ~ClientRegistry();

    void start();
    void stop();

    void broadcast_shrink(double fraction);
    void broadcast_grow(double fraction);

    size_t get_client_count() const;
    uint64_t get_total_cache_size() const;

private:
    void run_event_loop();
    void handle_new_connection(int listen_fd);
    void handle_client_message(int client_fd);
    void remove_client(int client_fd);
    void send_to_client(int fd, const Message& msg);
    
    std::string socket_path_;
    int listen_fd_{-1};
    int epoll_fd_{-1};
    
    std::thread epoll_thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex clients_mutex_;
    std::unordered_map<int, ClientInfo> clients_;
};

} // namespace governor
