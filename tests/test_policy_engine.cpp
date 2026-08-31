/**
 * Unit tests for the PolicyEngine — tests the ACTUAL implementation.
 * 
 * Uses synthetic PsiData traces to verify:
 *   - Correct tier transitions with hysteresis
 *   - Debounce prevents flapping
 *   - Full escalation and recovery cycle
 *   - Noisy signal stability
 */
#include "governor/policy_engine.h"
#include <iostream>
#include <string>
#include <cmath>
#include <vector>

// ─── Minimal test framework (no external deps) ─────────────────────────────

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_EQ(a, b)                                                       \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            std::cerr << "  FAIL: " << #a << " == " << #b                      \
                      << " (line " << __LINE__ << ")\n";                       \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(x)                                                        \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::cerr << "  FAIL: " << #x                                      \
                      << " (line " << __LINE__ << ")\n";                       \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define ASSERT_NE(a, b)                                                       \
    do {                                                                       \
        if ((a) == (b)) {                                                      \
            std::cerr << "  FAIL: " << #a << " != " << #b                      \
                      << " (line " << __LINE__ << ")\n";                       \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                          \
    do {                                                                       \
        tests_run++;                                                           \
        std::cout << "  " << #fn << " ... ";                                   \
        if (fn()) {                                                            \
            std::cout << "PASS\n";                                             \
            tests_passed++;                                                    \
        } else {                                                               \
            std::cout << "FAIL\n";                                             \
        }                                                                      \
    } while (0)

using namespace governor;

// ─── Helpers ────────────────────────────────────────────────────────────────

/** Build a PsiData with only some_avg10 set (the field the engine keys on). */
static PsiData make_psi(double avg10, double avg60 = 0.0) {
    PsiData d{};
    d.some_avg10  = avg10;
    d.some_avg60  = avg60;
    d.some_avg300 = 0.0;
    d.some_total  = 0;
    return d;
}

/** Feed a constant avg10 for N polls; return the last PolicyAction. */
static PolicyAction feed(PolicyEngine& eng, double avg10, int n) {
    PolicyAction last{};
    for (int i = 0; i < n; ++i)
        last = eng.evaluate(make_psi(avg10));
    return last;
}

// ─── Tests ──────────────────────────────────────────────────────────────────

bool test_initial_state() {
    PolicyEngine eng;
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);
    return true;
}

bool test_promote_to_elevated() {
    PolicyEngine eng;
    // Default: promote threshold for ELEVATED is 5.0, debounce = 3
    // Feed avg10=6.0 for exactly debounce polls → should promote
    PolicyAction act = feed(eng, 6.0, 3);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::ELEVATED);
    ASSERT_EQ(act.type, PolicyAction::Type::SHRINK);
    ASSERT_TRUE(act.fraction > 0.0);
    return true;
}

bool test_no_promote_below_threshold() {
    PolicyEngine eng;
    feed(eng, 4.0, 10);  // below 5.0
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);
    return true;
}

bool test_debounce_prevents_premature_promotion() {
    PolicyEngine eng;
    // Feed 2 polls above threshold, then drop — should NOT promote
    feed(eng, 6.0, 2);
    feed(eng, 3.0, 1);  // breaks the streak
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);

    // Even if we then feed above again for 1 poll
    feed(eng, 6.0, 1);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);
    return true;
}

bool test_promote_to_high() {
    PolicyEngine eng;
    feed(eng, 6.0, 3);   // → ELEVATED
    ASSERT_EQ(eng.get_current_tier(), PressureTier::ELEVATED);

    // HIGH promote threshold = 15.0, debounce = 3
    PolicyAction act = feed(eng, 16.0, 3);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::HIGH);
    ASSERT_EQ(act.type, PolicyAction::Type::SHRINK);
    return true;
}

bool test_promote_to_critical() {
    PolicyEngine eng;
    feed(eng, 6.0, 3);   // → ELEVATED
    feed(eng, 16.0, 3);  // → HIGH

    // CRITICAL promote threshold = 30.0, debounce = 2
    PolicyAction act = feed(eng, 31.0, 2);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::CRITICAL);
    ASSERT_EQ(act.type, PolicyAction::Type::SHRINK);
    return true;
}

bool test_demote_with_hysteresis() {
    PolicyEngine eng;
    feed(eng, 6.0, 3);   // → ELEVATED
    ASSERT_EQ(eng.get_current_tier(), PressureTier::ELEVATED);

    // ELEVATED demote threshold = 2.0. Feeding 3.0 (above 2.0) should NOT demote
    feed(eng, 3.0, 10);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::ELEVATED);

    // Feed below demote threshold (2.0) for debounce polls → should demote
    PolicyAction act = feed(eng, 1.5, 3);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);
    ASSERT_EQ(act.type, PolicyAction::Type::GROW);
    return true;
}

bool test_no_flapping() {
    PolicyEngine eng;
    // Alternate rapidly between just-above and just-below promote threshold
    for (int i = 0; i < 20; ++i) {
        eng.evaluate(make_psi(5.5));
        eng.evaluate(make_psi(4.5));
    }
    // Debounce should prevent any promotion
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);
    return true;
}

bool test_full_escalation_and_recovery() {
    PolicyEngine eng;
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);

    // Ramp up through all tiers
    feed(eng, 6.0, 3);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::ELEVATED);

    feed(eng, 16.0, 3);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::HIGH);

    feed(eng, 35.0, 2);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::CRITICAL);

    // Recover: CRITICAL demote threshold = 15.0
    feed(eng, 10.0, 2);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::HIGH);

    // HIGH demote threshold = 8.0
    feed(eng, 5.0, 3);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::ELEVATED);

    // ELEVATED demote threshold = 2.0
    feed(eng, 1.0, 3);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);
    return true;
}

bool test_noisy_signal() {
    PolicyEngine eng;
    // Moderate noise near ELEVATED promote threshold (5.0)
    // Mostly below → should stay NORMAL
    std::vector<double> noise = {4.8, 5.2, 4.7, 5.1, 4.6, 5.3, 4.9, 5.0, 4.5, 4.8};
    for (double v : noise)
        eng.evaluate(make_psi(v));
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);
    return true;
}

bool test_tier_name_strings() {
    ASSERT_EQ(PolicyEngine::get_tier_name(PressureTier::NORMAL),   std::string("NORMAL"));
    ASSERT_EQ(PolicyEngine::get_tier_name(PressureTier::ELEVATED), std::string("ELEVATED"));
    ASSERT_EQ(PolicyEngine::get_tier_name(PressureTier::HIGH),     std::string("HIGH"));
    ASSERT_EQ(PolicyEngine::get_tier_name(PressureTier::CRITICAL), std::string("CRITICAL"));
    return true;
}

bool test_shrink_fractions_correct() {
    PolicyEngine eng;
    // Promote to ELEVATED: shrink_pct should be 0.10
    PolicyAction act = feed(eng, 6.0, 3);
    ASSERT_TRUE(std::abs(act.fraction - 0.10) < 0.001);

    // Promote to HIGH: shrink_pct should be 0.25
    act = feed(eng, 16.0, 3);
    ASSERT_TRUE(std::abs(act.fraction - 0.25) < 0.001);

    // Promote to CRITICAL: shrink_pct should be 0.50
    act = feed(eng, 31.0, 2);
    ASSERT_TRUE(std::abs(act.fraction - 0.50) < 0.001);
    return true;
}

bool test_reset() {
    PolicyEngine eng;
    feed(eng, 6.0, 3);
    ASSERT_EQ(eng.get_current_tier(), PressureTier::ELEVATED);
    eng.reset();
    ASSERT_EQ(eng.get_current_tier(), PressureTier::NORMAL);
    return true;
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main() {
    std::cout << "PolicyEngine Tests\n";
    std::cout << "==================\n";

    RUN_TEST(test_initial_state);
    RUN_TEST(test_promote_to_elevated);
    RUN_TEST(test_no_promote_below_threshold);
    RUN_TEST(test_debounce_prevents_premature_promotion);
    RUN_TEST(test_promote_to_high);
    RUN_TEST(test_promote_to_critical);
    RUN_TEST(test_demote_with_hysteresis);
    RUN_TEST(test_no_flapping);
    RUN_TEST(test_full_escalation_and_recovery);
    RUN_TEST(test_noisy_signal);
    RUN_TEST(test_tier_name_strings);
    RUN_TEST(test_shrink_fractions_correct);
    RUN_TEST(test_reset);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
