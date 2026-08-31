#include "client/governor_client.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>

// ─── Construction / destruction ─────────────────────────────────────────────

GovernorClient::GovernorClient(const std::string& socket_path)
    : socket_path_(socket_path), sock_fd_(-1), connected_(false), listening_(false) {}

GovernorClient::~GovernorClient() {
    stop_listening();
    disconnect();
}

// ─── Connection ─────────────────────────────────────────────────────────────

bool GovernorClient::connect() {
    if (connected_) return true;

    sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd_ == -1) return false;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(sock_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }

    connected_ = true;
    return true;
}

void GovernorClient::disconnect() {
    if (sock_fd_ != -1) {
        close(sock_fd_);
        sock_fd_ = -1;
    }
    connected_ = false;
}

// ─── Send (uses shared protocol.h Message + framing) ────────────────────────

bool GovernorClient::send_message(const governor::Message& msg) {
    if (!connected_) return false;

    std::lock_guard<std::mutex> lock(send_mutex_);
    auto frame = governor::Message::frame(msg);

    size_t total_written = 0;
    while (total_written < frame.size()) {
        ssize_t n = write(sock_fd_, frame.data() + total_written,
                          frame.size() - total_written);
        if (n <= 0) {
            connected_ = false;
            return false;
        }
        total_written += static_cast<size_t>(n);
    }
    return true;
}

bool GovernorClient::register_cache(uint64_t current_size_bytes) {
    governor::Message msg;
    msg.type               = governor::MessageType::REGISTER;
    msg.pid                = getpid();
    msg.current_size_bytes = current_size_bytes;
    return send_message(msg);
}

bool GovernorClient::report_size(uint64_t current_size_bytes) {
    governor::Message msg;
    msg.type               = governor::MessageType::REPORT_SIZE;
    msg.pid                = getpid();
    msg.current_size_bytes = current_size_bytes;
    return send_message(msg);
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

void GovernorClient::on_shrink(std::function<void(uint64_t)> callback) {
    shrink_callback_ = std::move(callback);
}

void GovernorClient::on_grow(std::function<void(uint64_t)> callback) {
    grow_callback_ = std::move(callback);
}

// ─── Dispatch (uses shared protocol.h deserialization) ──────────────────────

void GovernorClient::process_message(const governor::Message& msg) {
    if (msg.type == governor::MessageType::SHRINK && shrink_callback_) {
        shrink_callback_(msg.target_bytes);
    } else if (msg.type == governor::MessageType::GROW && grow_callback_) {
        grow_callback_(msg.target_bytes);
    }
    // ACK / HEARTBEAT / UNKNOWN are silently ignored
}

// ─── Listener thread ────────────────────────────────────────────────────────

void GovernorClient::start_listening() {
    if (listening_) return;
    listening_ = true;
    listener_thread_ = std::thread(&GovernorClient::listener_loop, this);
}

void GovernorClient::stop_listening() {
    if (!listening_) return;
    listening_ = false;

    // Close socket to unblock the blocking read in the listener thread
    int old_fd = sock_fd_;
    sock_fd_ = -1;
    if (old_fd != -1) close(old_fd);
    connected_ = false;

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
}

bool GovernorClient::is_connected() const {
    return connected_;
}

void GovernorClient::listener_loop() {
    int backoff_seconds = 1;

    while (listening_) {
        // Reconnect with exponential backoff if disconnected
        if (!connected_) {
            if (connect()) {
                backoff_seconds = 1;  // reset backoff on success
            } else {
                std::this_thread::sleep_for(std::chrono::seconds(backoff_seconds));
                backoff_seconds = std::min(30, backoff_seconds * 2);
                continue;
            }
        }

        // Read 4-byte length prefix (network byte order)
        uint32_t net_len;
        ssize_t bytes_read = read(sock_fd_, &net_len, sizeof(net_len));

        if (bytes_read == 0) {
            // EOF — daemon closed connection
            connected_ = false;
            continue;
        } else if (bytes_read != static_cast<ssize_t>(sizeof(net_len))) {
            connected_ = false;
            continue;
        }

        uint32_t len = ntohl(net_len);
        if (len == 0 || len > 65536) {
            // Sanity check: reject absurdly large or empty frames
            connected_ = false;
            continue;
        }

        // Read the JSON payload
        std::string payload(len, '\0');
        size_t total_read = 0;
        while (total_read < len) {
            bytes_read = read(sock_fd_, &payload[total_read], len - total_read);
            if (bytes_read <= 0) {
                connected_ = false;
                break;
            }
            total_read += static_cast<size_t>(bytes_read);
        }

        if (connected_) {
            // Deserialize using shared protocol.h
            governor::Message msg = governor::Message::deserialize(payload);
            process_message(msg);
        }
    }
}
