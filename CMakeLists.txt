cmake_minimum_required(VERSION 3.16)
project(memory_aware_governor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -O2)

find_package(Threads REQUIRED)

include_directories(include)

# Option for tests
option(BUILD_TESTS "Build tests" ON)

# 1. governor_daemon
add_executable(governor_daemon 
    src/governor/psi_poller.cpp
    src/governor/policy_engine.cpp
    src/governor/client_registry.cpp
    src/governor/governor_daemon.cpp
    src/governor/main.cpp
)
target_link_libraries(governor_daemon PRIVATE Threads::Threads)

# 2. governor_client static library
add_library(governor_client STATIC 
    src/client/governor_client.cpp
)
target_link_libraries(governor_client PRIVATE Threads::Threads)

# 3. governed_cache static library
add_library(governed_cache STATIC 
    src/client/governed_cache.cpp
)
target_link_libraries(governed_cache PRIVATE governor_client)

# 4. cache_app executable
add_executable(cache_app 
    src/cache_app/main.cpp
)
target_link_libraries(cache_app PRIVATE governed_cache governor_client Threads::Threads)

# 5. pressure_gen executable
add_executable(pressure_gen 
    src/pressure_gen/main.cpp
)
target_link_libraries(pressure_gen PRIVATE Threads::Threads)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
install(TARGETS governor_daemon cache_app pressure_gen
        DESTINATION bin)
install(TARGETS governor_client governed_cache
        DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
