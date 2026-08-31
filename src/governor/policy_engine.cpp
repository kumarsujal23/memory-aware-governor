#include "governor/policy_engine.h"
#include <algorithm>

namespace governor {

std::vector<TierConfig> PolicyEngine::default_configs() {
    return {
        // promote_threshold, demote_threshold, debounce_polls, shrink_pct
        {5.0,  0.0,  3, 0.0},   // NORMAL   — no shrink at this tier
        {5.0,  2.0,  3, 0.10},  // ELEVATED — shrink 10%
        {15.0, 8.0,  3, 0.25},  // HIGH     — shrink 25%
        {30.0, 15.0, 2, 0.50}   // CRITICAL — shrink 50%
    };
}

PolicyEngine::PolicyEngine(std::vector<TierConfig> configs) 
    : configs_(std::move(configs)) {
    if (configs_.empty()) {
        configs_ = default_configs();
    }
}

PressureTier PolicyEngine::get_current_tier() const {
    return current_tier_;
}

void PolicyEngine::reset() {
    current_tier_ = PressureTier::NORMAL;
    consecutive_promote_polls_ = 0;
    consecutive_demote_polls_ = 0;
}

std::string PolicyEngine::get_tier_name(PressureTier tier) {
    switch (tier) {
        case PressureTier::NORMAL:   return "NORMAL";
        case PressureTier::ELEVATED: return "ELEVATED";
        case PressureTier::HIGH:     return "HIGH";
        case PressureTier::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

PolicyAction PolicyEngine::evaluate(const PsiData& data) {
    PolicyAction action;
    action.type     = PolicyAction::Type::NONE;
    action.new_tier = current_tier_;
    action.fraction = 0.0;

    int current_idx = static_cast<int>(current_tier_);
    double psi_avg10 = data.some_avg10;
    double psi_avg60 = data.some_avg60;

    bool promoted = false;

    // ── Promotion check ─────────────────────────────────────────────────────
    // We check the *next* tier's promote_threshold against the current PSI.
    // Special case for CRITICAL (tier 3): also trigger if avg60 is elevated
    // (>= half the promote threshold), per spec:
    //   "avg10 >= 30%  OR  avg60 also elevated"
    if (current_idx < static_cast<int>(configs_.size()) - 1) {
        int next_idx = current_idx + 1;
        const auto& next_config = configs_[next_idx];

        // Determine if the promote condition is met.
        // For the CRITICAL tier, also accept elevated avg60 as a trigger.
        bool promote_condition = (psi_avg10 >= next_config.promote_threshold);
        if (next_idx == static_cast<int>(PressureTier::CRITICAL)) {
            // avg60 elevated means: avg60 >= the promote threshold of the
            // *current* tier (i.e., we've been under sustained high pressure
            // long enough for the 60-second window to reflect it too).
            bool avg60_elevated = (psi_avg60 >= configs_[current_idx].promote_threshold);
            promote_condition = promote_condition || avg60_elevated;
        }

        if (promote_condition) {
            consecutive_promote_polls_++;
            consecutive_demote_polls_ = 0;

            if (consecutive_promote_polls_ >= next_config.debounce_polls) {
                current_tier_ = static_cast<PressureTier>(next_idx);
                action.type     = PolicyAction::Type::SHRINK;
                action.fraction = next_config.shrink_pct;
                action.new_tier = current_tier_;
                consecutive_promote_polls_ = 0;
                promoted = true;
            }
        } else {
            consecutive_promote_polls_ = 0;
        }
    }

    // ── Demotion check (only if we did not just promote) ────────────────────
    // Demotion requires pressure to drop below the *current* tier's demote
    // threshold (which is strictly lower than its promote threshold —
    // hysteresis band) for debounce consecutive polls.
    if (!promoted && current_idx > 0) {
        const auto& curr_config = configs_[current_idx];

        if (psi_avg10 < curr_config.demote_threshold) {
            consecutive_demote_polls_++;
            consecutive_promote_polls_ = 0;

            if (consecutive_demote_polls_ >= curr_config.debounce_polls) {
                current_tier_ = static_cast<PressureTier>(current_idx - 1);
                action.type     = PolicyAction::Type::GROW;
                action.fraction = curr_config.shrink_pct;  // grow back same fraction
                action.new_tier = current_tier_;
                consecutive_demote_polls_ = 0;
            }
        } else {
            consecutive_demote_polls_ = 0;
        }
    }

    return action;
}

} // namespace governor
