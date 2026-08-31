#pragma once

#include "governor/psi_poller.h"
#include "governor/policy_engine.h"
#include "governor/client_registry.h"
#include <string>
#include <memory>
#include <vector>

namespace governor {

struct DaemonConfig {
    std::string psi_path{"/proc/pressure/memory"};
    std::string socket_path{"/tmp/mem_governor.sock"};
    int poll_interval_ms{200};
    std::vector<TierConfig> tier_configs{PolicyEngine::default_configs()};
};

class GovernorDaemon {
public:
    GovernorDaemon(const DaemonConfig& config);
    ~GovernorDaemon();

    void start();
    void stop();

private:
    void on_psi_update(const PsiData& data);
    void dispatch_action(const PolicyAction& action);

    DaemonConfig config_;
    std::unique_ptr<PsiPoller> poller_;
    std::unique_ptr<PolicyEngine> engine_;
    std::unique_ptr<ClientRegistry> registry_;
};

} // namespace governor
