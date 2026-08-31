#pragma once

#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

namespace governor {

struct PsiData {
    double some_avg10{0.0};
    double some_avg60{0.0};
    double some_avg300{0.0};
    uint64_t some_total{0};
    
    double full_avg10{0.0};
    double full_avg60{0.0};
    double full_avg300{0.0};
    uint64_t full_total{0};
};

class PsiPoller {
public:
    PsiPoller(std::string path = "/proc/pressure/memory", int poll_interval_ms = 200);
    ~PsiPoller();

    void start();
    void stop();
    
    PsiData get_latest() const;
    void set_callback(std::function<void(const PsiData&)> cb);
    
    static PsiData parse_psi_file(const std::string& content);

private:
    void poll_loop();
    std::string read_file(const std::string& path);

    std::string path_;
    int poll_interval_ms_;
    
    std::thread thread_;
    std::atomic<bool> running_{false};
    
    mutable std::mutex mutex_;
    PsiData latest_data_;
    
    std::function<void(const PsiData&)> callback_;
};

} // namespace governor
