#include "governor/client_registry.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <algorithm>

namespace governor {

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

ClientRegistry::ClientRegistry(std::string socket_path)
    : socket_path_(std::move(socket_path)) {}

ClientRegistry::~ClientRegistry() {
    stop();
}

void ClientRegistry::start() {
    if (running_.exchange(true)) return;

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ == -1) {
        throw std::runtime_error("Failed to create socket");
    }

    set_nonblocking(listen_fd_);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    unlink(socket_path_.c_str());

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(listen_fd_);
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(listen_fd_, 128) == -1) {
        close(listen_fd_);
        throw std::runtime_error("Failed to listen on socket");
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        close(listen_fd_);
        throw std::runtime_error("Failed to create epoll fd");
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) == -1) {
        close(listen_fd_);
        close(epoll_fd_);
        throw std::runtime_error("Failed to add listen fd to epoll");
    }

    epoll_thread_ = std::thread(&ClientRegistry::run_event_loop, this);
}

void ClientRegistry::stop() {
    if (!running_.exchange(false)) return;

    if (epoll_thread_.joinable()) {
        epoll_thread_.join();
    }

    if (listen_fd_ != -1) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }

    unlink(socket_path_.c_str());

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& pair : clients_) {
        close(pair.first);
    }
    clients_.clear();
}

void ClientRegistry::run_event_loop() {
    constexpr int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    while (running_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listen_fd_) {
                handle_new_connection(listen_fd_);
            } else {
                if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
                    remove_client(events[i].data.fd);
                } else {
                    handle_client_message(events[i].data.fd);
                }
            }
        }
    }
}

void ClientRegistry::handle_new_connection(int listen_fd) {
    while (true) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                continue;
            }
        }

        set_nonblocking(client_fd);

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
            close(client_fd);
            continue;
        }

        std::lock_guard<std::mutex> lock(clients_mutex_);
        ClientInfo info;
        info.fd = client_fd;
        info.pid = 0;
        info.current_size_bytes = 0;
        info.min_size_bytes = 0;
        info.connected_at = std::chrono::steady_clock::now();
        clients_[client_fd] = info;
    }
}

void ClientRegistry::handle_client_message(int client_fd) {
    std::vector<uint8_t> buffer(4096);
    while (true) {
        uint32_t msg_len_net;
        ssize_t n = read(client_fd, &msg_len_net, sizeof(msg_len_net));
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            remove_client(client_fd);
            return;
        } else if (n == 0) {
            remove_client(client_fd);
            return;
        } else if (n < (ssize_t)sizeof(msg_len_net)) {
            // Partial read for length not handled for brevity
            continue;
        }

        uint32_t msg_len = ntohl(msg_len_net);
        if (msg_len > buffer.size()) {
            buffer.resize(msg_len);
        }

        uint32_t total_read = 0;
        while (total_read < msg_len) {
            n = read(client_fd, buffer.data() + total_read, msg_len - total_read);
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue; 
                }
                remove_client(client_fd);
                return;
            } else if (n == 0) {
                remove_client(client_fd);
                return;
            }
            total_read += n;
        }

        std::string json(reinterpret_cast<char*>(buffer.data()), msg_len);
        
        try {
            Message msg = Message::deserialize(json);
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto it = clients_.find(client_fd);
            if (it != clients_.end()) {
                if (msg.type == MessageType::REGISTER) {
                    it->second.pid = msg.pid;
                    it->second.current_size_bytes = msg.current_size_bytes;
                } else if (msg.type == MessageType::REPORT_SIZE) {
                    it->second.current_size_bytes = msg.current_size_bytes;
                }
            }
        } catch (...) {
            // Ignore parse errors
        }
    }
}

void ClientRegistry::remove_client(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = clients_.find(client_fd);
    if (it != clients_.end()) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
        clients_.erase(it);
    }
}

void ClientRegistry::send_to_client(int fd, const Message& msg) {
    auto frame = Message::frame(msg);
    size_t total_written = 0;
    while (total_written < frame.size()) {
        ssize_t n = write(fd, frame.data() + total_written, frame.size() - total_written);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        total_written += n;
    }
}

void ClientRegistry::broadcast_shrink(double fraction) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& pair : clients_) {
        Message msg;
        msg.type = MessageType::SHRINK;
        msg.target_bytes = pair.second.current_size_bytes * (1.0 - fraction);
        send_to_client(pair.first, msg);
    }
}

void ClientRegistry::broadcast_grow(double fraction) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& pair : clients_) {
        Message msg;
        msg.type = MessageType::GROW;
        msg.target_bytes = pair.second.current_size_bytes * (1.0 + fraction);
        send_to_client(pair.first, msg);
    }
}

size_t ClientRegistry::get_client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

uint64_t ClientRegistry::get_total_cache_size() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    uint64_t total = 0;
    for (const auto& pair : clients_) {
        total += pair.second.current_size_bytes;
    }
    return total;
}

} // namespace governor
