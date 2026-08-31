/**
 * Unit tests for PsiPoller::parse_psi_file() — tests the ACTUAL parser.
 * 
 * Validates parsing of /proc/pressure/memory format strings into PsiData structs.
 */
#include "governor/psi_poller.h"
#include <iostream>
#include <string>
#include <cmath>

// ─── Minimal test framework ────────────────────────────────────────────────

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_NEAR(a, b, eps)                                                \
    do {                                                                       \
        if (std::abs((a) - (b)) > (eps)) {                                     \
            std::cerr << "  FAIL: " << #a << " ≈ " << #b                       \
                      << " (got " << (a) << " vs " << (b) << ", eps=" << (eps) \
                      << ") at line " << __LINE__ << "\n";                     \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b)                                                       \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            std::cerr << "  FAIL: " << #a << " == " << #b                      \
                      << " (got " << (a) << " vs " << (b)                      \
                      << ") at line " << __LINE__ << "\n";                     \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(x)                                                        \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::cerr << "  FAIL: " << #x                                      \
                      << " at line " << __LINE__ << "\n";                      \
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

// ─── Tests ──────────────────────────────────────────────────────────────────

bool test_parse_valid() {
    std::string content =
        "some avg10=2.50 avg60=1.20 avg300=0.80 total=123456\n"
        "full avg10=1.10 avg60=0.50 avg300=0.10 total=7890\n";

    PsiData data = PsiPoller::parse_psi_file(content);

    ASSERT_NEAR(data.some_avg10,  2.50, 0.001);
    ASSERT_NEAR(data.some_avg60,  1.20, 0.001);
    ASSERT_NEAR(data.some_avg300, 0.80, 0.001);
    ASSERT_EQ(data.some_total, static_cast<uint64_t>(123456));

    ASSERT_NEAR(data.full_avg10,  1.10, 0.001);
    ASSERT_NEAR(data.full_avg60,  0.50, 0.001);
    ASSERT_NEAR(data.full_avg300, 0.10, 0.001);
    ASSERT_EQ(data.full_total, static_cast<uint64_t>(7890));
    return true;
}

bool test_parse_zeros() {
    std::string content =
        "some avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"
        "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n";

    PsiData data = PsiPoller::parse_psi_file(content);

    ASSERT_NEAR(data.some_avg10, 0.0, 0.001);
    ASSERT_NEAR(data.some_avg60, 0.0, 0.001);
    ASSERT_EQ(data.some_total, static_cast<uint64_t>(0));
    ASSERT_NEAR(data.full_avg10, 0.0, 0.001);
    return true;
}

bool test_parse_high_values() {
    std::string content =
        "some avg10=99.99 avg60=80.50 avg300=50.00 total=9999999999\n"
        "full avg10=75.00 avg60=60.00 avg300=40.00 total=8888888888\n";

    PsiData data = PsiPoller::parse_psi_file(content);

    ASSERT_NEAR(data.some_avg10, 99.99, 0.001);
    ASSERT_EQ(data.some_total, static_cast<uint64_t>(9999999999ULL));
    ASSERT_NEAR(data.full_avg10, 75.00, 0.001);
    return true;
}

bool test_parse_malformed() {
    // Completely invalid format — parser should return zeroed data, not crash
    std::string content = "invalid format string\ngarbage data here\n";
    PsiData data = PsiPoller::parse_psi_file(content);

    // All fields should remain at default (0)
    ASSERT_NEAR(data.some_avg10, 0.0, 0.001);
    ASSERT_EQ(data.some_total, static_cast<uint64_t>(0));
    return true;
}

bool test_parse_partial() {
    // Only "some" line present, no "full" line
    std::string content = "some avg10=5.50 avg60=3.20 avg300=1.10 total=54321\n";
    PsiData data = PsiPoller::parse_psi_file(content);

    ASSERT_NEAR(data.some_avg10, 5.50, 0.001);
    ASSERT_EQ(data.some_total, static_cast<uint64_t>(54321));

    // Full fields should remain 0
    ASSERT_NEAR(data.full_avg10, 0.0, 0.001);
    ASSERT_EQ(data.full_total, static_cast<uint64_t>(0));
    return true;
}

bool test_parse_extra_whitespace() {
    // Ensure parser handles typical kernel output with varied spacing
    std::string content =
        "some avg10=10.00 avg60=5.00 avg300=2.00 total=100000\n"
        "full avg10=4.00 avg60=2.00 avg300=1.00 total=50000\n";

    PsiData data = PsiPoller::parse_psi_file(content);
    ASSERT_NEAR(data.some_avg10, 10.0, 0.001);
    ASSERT_NEAR(data.full_avg10, 4.0, 0.001);
    return true;
}

bool test_parse_empty_string() {
    PsiData data = PsiPoller::parse_psi_file("");
    ASSERT_NEAR(data.some_avg10, 0.0, 0.001);
    ASSERT_EQ(data.some_total, static_cast<uint64_t>(0));
    return true;
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main() {
    std::cout << "PsiParser Tests\n";
    std::cout << "===============\n";

    RUN_TEST(test_parse_valid);
    RUN_TEST(test_parse_zeros);
    RUN_TEST(test_parse_high_values);
    RUN_TEST(test_parse_malformed);
    RUN_TEST(test_parse_partial);
    RUN_TEST(test_parse_extra_whitespace);
    RUN_TEST(test_parse_empty_string);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
