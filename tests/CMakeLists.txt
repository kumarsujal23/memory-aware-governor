cmake_minimum_required(VERSION 3.16)

# Tests inherit C++ standard and include dirs from the parent project.
# Each test links only the source files it actually needs.

# ── test_policy_engine ────────────────────────────────────────────────────────
# Depends on: policy_engine.cpp, psi_poller.h (PsiData struct is header-only)
add_executable(test_policy_engine
    test_policy_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/governor/policy_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/governor/psi_poller.cpp
)
target_include_directories(test_policy_engine PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_policy_engine PRIVATE Threads::Threads)

# ── test_psi_parser ───────────────────────────────────────────────────────────
# Depends on: psi_poller.cpp (for parse_psi_file)
add_executable(test_psi_parser
    test_psi_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/governor/psi_poller.cpp
)
target_include_directories(test_psi_parser PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_psi_parser PRIVATE Threads::Threads)

# ── test_lru_cache ────────────────────────────────────────────────────────────
# LRUCache is a header-only template in governed_cache.h; no .cpp link needed
add_executable(test_lru_cache
    test_lru_cache.cpp
)
target_include_directories(test_lru_cache PRIVATE ${CMAKE_SOURCE_DIR}/include)

# ── CTest integration ────────────────────────────────────────────────────────
enable_testing()
add_test(NAME PolicyEngineTest COMMAND test_policy_engine)
add_test(NAME PsiParserTest    COMMAND test_psi_parser)
add_test(NAME LruCacheTest     COMMAND test_lru_cache)
