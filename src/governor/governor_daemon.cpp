#include "governor/governor_daemon.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <sstream>

namespace governor {

GovernorDaemon::GovernorDaemon(const DaemonConfig& config)
    : config_(config) {
    poller_   = std::make_unique<PsiPoller>(config.psi_path, config.poll_interval_ms);
    engine_   = std::make_unique<PolicyEngine>(config.tier_configs);
    registry_ = std::make_unique<ClientRegistry>(config.socket_path);

    poller_->set_callback([this](const PsiData& data) {
        this->on_psi_update(data);
    });
}

GovernorDaemon::~GovernorDaemon() {
    stop();
}

void GovernorDaemon::start() {
    registry_->start();
    poller_->start();
    std::cout << "[Daemon] Governor started | socket=" << config_.socket_path
              << " psi=" << config_.psi_path
              << " poll_interval=" << config_.poll_interval_ms << "ms\n";
}

void GovernorDaemon::stop() {
    poller_->stop();
    registry_->stop();
    std::cout << "[Daemon] Governor stopped\n";
}

void GovernorDaemon::on_psi_update(const PsiData& data) {
    PressureTier old_tier = engine_->get_current_tier();
    PolicyAction action   = engine_->evaluate(data);
    PressureTier new_tier = engine_->get_current_tier();

    // Log every tier transition with timestamp and full PSI snapshot
    if (old_tier != new_tier) {
        // Format timestamp without trailing newline (std::ctime appends \n)
        auto now   = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
        localtime_r(&now_t, &tm_buf);

        std::ostringstream ts;
        ts << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

        std::cout << "[Daemon] [" << ts.str() << "] "
                  << "TIER TRANSITION: "
                  << PolicyEngine::get_tier_name(old_tier) << " -> "
                  << PolicyEngine::get_tier_name(new_tier)
                  << " | some: avg10=" << std::fixed << std::setprecision(2) << data.some_avg10
                  << " avg60=" << data.some_avg60
                  << " avg300=" << data.some_avg300
                  << " total=" << data.some_total
                  << " | full: avg10=" << data.full_avg10
                  << " avg60=" << data.full_avg60
                  << " | clients=" << registry_->get_client_count()
                  << " total_cache=" << registry_->get_total_cache_size() << " bytes"
                  << "\n";
    }

    if (action.type != PolicyAction::Type::NONE) {
        dispatch_action(action);
    }
}

void GovernorDaemon::dispatch_action(const PolicyAction& action) {
    if (action.type == PolicyAction::Type::SHRINK) {
        std::cout << "[Daemon] ACTION: SHRINK " << std::fixed << std::setprecision(0)
                  << (action.fraction * 100.0) << "% across "
                  << registry_->get_client_count() << " client(s)\n";
        registry_->broadcast_shrink(action.fraction);
    } else if (action.type == PolicyAction::Type::GROW) {
        std::cout << "[Daemon] ACTION: GROW " << std::fixed << std::setprecision(0)
                  << (action.fraction * 100.0) << "% across "
                  << registry_->get_client_count() << " client(s)\n";
        registry_->broadcast_grow(action.fraction);
    }
}

} // namespace governor
