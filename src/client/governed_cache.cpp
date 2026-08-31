#include "client/governed_cache.h"
#include <iostream>
#include <chrono>

GovernedCache::GovernedCache(size_t max_capacity, const std::string& socket_path, size_t estimated_entry_bytes)
    : cache_(std::make_unique<LRUCache<std::string, std::string>>(max_capacity)),
      client_(socket_path),
      estimated_entry_bytes_(estimated_entry_bytes) {
          
    client_.on_shrink([this](uint64_t target_bytes) {
        this->handle_shrink(target_bytes);
    });
    
    client_.on_grow([this](uint64_t target_bytes) {
        this->handle_grow(target_bytes);
    });
}

GovernedCache::~GovernedCache() {
    stop();
}

void GovernedCache::start() {
    if (client_.connect()) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t current_size_bytes = cache_->size() * estimated_entry_bytes_;
        client_.register_cache(current_size_bytes);
        client_.start_listening();
    } else {
        std::cerr << "GovernedCache: Failed to connect to governor daemon." << std::endl;
    }
}

void GovernedCache::stop() {
    client_.stop_listening();
    client_.disconnect();
}

std::optional<std::string> GovernedCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_->get(key);
}

void GovernedCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_->put(key, value);
    
    // Periodically we might want to report size, but doing it on every put is too expensive.
    // The application can handle periodic reporting.
}

CacheStats GovernedCache::get_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    return {
        cache_->size(),
        cache_->capacity(),
        cache_->hit_rate(),
        cache_->get_hits(),
        cache_->get_misses()
    };
}

void GovernedCache::handle_shrink(uint64_t target_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t target_capacity = target_bytes / estimated_entry_bytes_;
    
    size_t old_size = cache_->size();
    size_t old_capacity = cache_->capacity();
    
    // We update capacity and evict
    if (target_capacity < cache_->capacity()) {
        cache_->resize(target_capacity);
        
        std::cout << "[GovernedCache] SHRINK applied: " 
                  << "Old Capacity: " << old_capacity << " -> New Capacity: " << target_capacity 
                  << " | Old Size: " << old_size << " -> New Size: " << cache_->size() << std::endl;
                  
        client_.report_size(cache_->size() * estimated_entry_bytes_);
    }
}

void GovernedCache::handle_grow(uint64_t target_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t target_capacity = target_bytes / estimated_entry_bytes_;
    
    size_t old_capacity = cache_->capacity();
    
    if (target_capacity > cache_->capacity()) {
        cache_->resize(target_capacity); // Will just update capacity_ since it's a grow
        
        std::cout << "[GovernedCache] GROW applied: " 
                  << "Old Capacity: " << old_capacity << " -> New Capacity: " << target_capacity << std::endl;
                  
        client_.report_size(cache_->size() * estimated_entry_bytes_);
    }
}
