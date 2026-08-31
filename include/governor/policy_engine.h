#pragma once

#include "governor/psi_poller.h"
#include <vector>
#include <string>

namespace governor {

enum class PressureTier {
    NORMAL = 0,
    ELEVATED = 1,
    HIGH = 2,
    CRITICAL = 3
};

struct TierConfig {
    double promote_threshold;
    double demote_threshold;
    int debounce_polls;
    double shrink_pct;
};

struct PolicyAction {
    enum class Type { NONE, SHRINK, GROW };
    Type type{Type::NONE};
    double fraction{0.0};
    PressureTier new_tier{PressureTier::NORMAL};
};

class PolicyEngine {
public:
    PolicyEngine(std::vector<TierConfig> configs = default_configs());

    PolicyAction evaluate(const PsiData& data);
    PressureTier get_current_tier() const;
    void reset();
    
    static std::string get_tier_name(PressureTier tier);
    static std::vector<TierConfig> default_configs();

private:
    std::vector<TierConfig> configs_;
    PressureTier current_tier_{PressureTier::NORMAL};
    int consecutive_promote_polls_{0};
    int consecutive_demote_polls_{0};
};

} // namespace governor
