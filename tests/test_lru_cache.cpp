/**
 * Unit tests for LRUCache — tests the ACTUAL template implementation from governed_cache.h.
 *
 * Validates O(1) get/put/evict, LRU ordering, resize, hit rate tracking, and
 * large-scale correctness.
 */
#include "client/governed_cache.h"   // pulls in LRUCache<K,V>
#include <iostream>
#include <string>
#include <cmath>

// ─── Minimal test framework ────────────────────────────────────────────────

static int tests_run = 0;
static int tests_passed = 0;

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

#define ASSERT_NEAR(a, b, eps)                                                \
    do {                                                                       \
        if (std::abs((a) - (b)) > (eps)) {                                     \
            std::cerr << "  FAIL: " << #a << " ≈ " << #b                       \
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

// ─── Tests ──────────────────────────────────────────────────────────────────

bool test_basic_put_get() {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    auto val = cache.get(1);
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val.value(), 10);
    return true;
}

bool test_capacity_eviction() {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);  // should evict key 1 (LRU)
    ASSERT_TRUE(!cache.get(1).has_value());
    ASSERT_TRUE(cache.get(2).has_value());
    ASSERT_TRUE(cache.get(3).has_value());
    return true;
}

bool test_access_updates_lru_order() {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.get(1);      // touch key 1 → now MRU
    cache.put(3, 30);  // should evict key 2 (now LRU)
    ASSERT_TRUE(cache.get(1).has_value());
    ASSERT_TRUE(!cache.get(2).has_value());
    ASSERT_TRUE(cache.get(3).has_value());
    return true;
}

bool test_resize_down() {
    LRUCache<int, int> cache(5);
    for (int i = 0; i < 5; ++i)
        cache.put(i, i * 10);

    ASSERT_EQ(cache.size(), static_cast<size_t>(5));
    cache.resize(2);  // evict 3 LRU items (keys 0, 1, 2)
    ASSERT_EQ(cache.size(), static_cast<size_t>(2));

    // Keys 3 and 4 (most recently used) should survive
    ASSERT_TRUE(cache.get(3).has_value());
    ASSERT_TRUE(cache.get(4).has_value());
    ASSERT_TRUE(!cache.get(0).has_value());
    return true;
}

bool test_resize_up() {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.resize(5);   // grow capacity

    cache.put(3, 30);
    cache.put(4, 40);
    cache.put(5, 50);

    ASSERT_EQ(cache.size(), static_cast<size_t>(5));
    ASSERT_TRUE(cache.get(1).has_value());
    ASSERT_TRUE(cache.get(5).has_value());
    return true;
}

bool test_hit_rate_tracking() {
    LRUCache<int, int> cache(5);
    cache.put(1, 10);
    cache.put(2, 20);

    cache.get(1);  // hit
    cache.get(2);  // hit
    cache.get(3);  // miss
    cache.get(4);  // miss

    // 2 hits out of 4 total accesses = 50%
    ASSERT_NEAR(cache.hit_rate(), 0.5, 0.01);
    return true;
}

bool test_overwrite_existing() {
    LRUCache<int, int> cache(3);
    cache.put(1, 10);
    cache.put(2, 20);
    ASSERT_EQ(cache.size(), static_cast<size_t>(2));

    cache.put(1, 15);  // overwrite
    ASSERT_EQ(cache.size(), static_cast<size_t>(2));  // size unchanged
    ASSERT_EQ(cache.get(1).value(), 15);
    return true;
}

bool test_evict_to() {
    LRUCache<int, int> cache(10);
    for (int i = 0; i < 10; ++i)
        cache.put(i, i);

    cache.evict_to(3);
    ASSERT_EQ(cache.size(), static_cast<size_t>(3));

    // Most recent keys (7, 8, 9) should survive
    ASSERT_TRUE(cache.get(7).has_value());
    ASSERT_TRUE(cache.get(8).has_value());
    ASSERT_TRUE(cache.get(9).has_value());
    ASSERT_TRUE(!cache.get(0).has_value());
    return true;
}

bool test_empty_cache_get() {
    LRUCache<int, int> cache(5);
    auto val = cache.get(42);
    ASSERT_TRUE(!val.has_value());
    return true;
}

bool test_reset_stats() {
    LRUCache<int, int> cache(5);
    cache.put(1, 10);
    cache.get(1);     // hit
    cache.get(999);   // miss
    ASSERT_TRUE(cache.hit_rate() > 0.0);

    cache.reset_stats();
    ASSERT_NEAR(cache.hit_rate(), 0.0, 0.001);
    ASSERT_EQ(cache.get_hits(), static_cast<uint64_t>(0));
    ASSERT_EQ(cache.get_misses(), static_cast<uint64_t>(0));
    return true;
}

bool test_large_scale() {
    const size_t CAPACITY = 50000;
    LRUCache<int, int> cache(CAPACITY);

    for (int i = 0; i < 100000; ++i)
        cache.put(i, i);

    ASSERT_EQ(cache.size(), CAPACITY);

    // Keys 0..49999 should have been evicted
    ASSERT_TRUE(!cache.get(0).has_value());
    ASSERT_TRUE(!cache.get(49999).has_value());

    // Keys 50000..99999 should be present
    ASSERT_TRUE(cache.get(50000).has_value());
    ASSERT_TRUE(cache.get(99999).has_value());
    return true;
}

bool test_string_keys() {
    LRUCache<std::string, std::string> cache(3);
    cache.put("alpha", "value_a");
    cache.put("beta",  "value_b");
    cache.put("gamma", "value_c");

    ASSERT_EQ(cache.get("alpha").value(), std::string("value_a"));
    cache.put("delta", "value_d");  // evicts beta (LRU after alpha was touched)

    ASSERT_TRUE(!cache.get("beta").has_value());
    ASSERT_TRUE(cache.get("gamma").has_value());
    return true;
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main() {
    std::cout << "LRUCache Tests\n";
    std::cout << "==============\n";

    RUN_TEST(test_basic_put_get);
    RUN_TEST(test_capacity_eviction);
    RUN_TEST(test_access_updates_lru_order);
    RUN_TEST(test_resize_down);
    RUN_TEST(test_resize_up);
    RUN_TEST(test_hit_rate_tracking);
    RUN_TEST(test_overwrite_existing);
    RUN_TEST(test_evict_to);
    RUN_TEST(test_empty_cache_get);
    RUN_TEST(test_reset_stats);
    RUN_TEST(test_large_scale);
    RUN_TEST(test_string_keys);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
