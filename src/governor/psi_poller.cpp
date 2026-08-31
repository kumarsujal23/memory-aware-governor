#include "governor/psi_poller.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <iostream>

namespace governor {

PsiPoller::PsiPoller(std::string path, int poll_interval_ms)
    : path_(std::move(path)), poll_interval_ms_(poll_interval_ms) {}

PsiPoller::~PsiPoller() {
    stop();
}

void PsiPoller::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&PsiPoller::poll_loop, this);
}

void PsiPoller::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) {
        thread_.join();
    }
}

PsiData PsiPoller::get_latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_data_;
}

void PsiPoller::set_callback(std::function<void(const PsiData&)> cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(cb);
}

std::string PsiPoller::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void PsiPoller::poll_loop() {
    while (running_) {
        std::string content = read_file(path_);
        if (!content.empty()) {
            PsiData data = parse_psi_file(content);
            
            std::function<void(const PsiData&)> cb;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                latest_data_ = data;
                cb = callback_;
            }
            if (cb) {
                cb(data);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
    }
}

PsiData PsiPoller::parse_psi_file(const std::string& content) {
    PsiData data;
    std::istringstream iss(content);
    std::string line;
    
    auto parse_line = [](const std::string& l, double& avg10, double& avg60, double& avg300, uint64_t& total) {
        std::istringstream ls(l);
        std::string prefix, token;
        ls >> prefix; // "some" or "full"
        while (ls >> token) {
            size_t eq = token.find('=');
            if (eq != std::string::npos) {
                std::string key = token.substr(0, eq);
                std::string val = token.substr(eq + 1);
                if (key == "avg10") avg10 = std::stod(val);
                else if (key == "avg60") avg60 = std::stod(val);
                else if (key == "avg300") avg300 = std::stod(val);
                else if (key == "total") total = std::stoull(val);
            }
        }
    };

    while (std::getline(iss, line)) {
        if (line.rfind("some", 0) == 0) {
            parse_line(line, data.some_avg10, data.some_avg60, data.some_avg300, data.some_total);
        } else if (line.rfind("full", 0) == 0) {
            parse_line(line, data.full_avg10, data.full_avg60, data.full_avg300, data.full_total);
        }
    }
    return data;
}

} // namespace governor
