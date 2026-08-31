#pragma once

#include <unordered_map>
#include <optional>
#include <cstdint>
#include <string>
#include <mutex>
#include <memory>
#include "client/governor_client.h"

template <typename Key, typename Value>
class LRUCache {
public:
    struct Node {
        Key key;
        Value value;
        Node* prev;
        Node* next;
        
        Node(Key k, Value v) : key(std::move(k)), value(std::move(v)), prev(nullptr), next(nullptr) {}
    };

    explicit LRUCache(size_t max_capacity) : capacity_(max_capacity), size_(0), head_(nullptr), tail_(nullptr), hits_(0), misses_(0) {}
    
    ~LRUCache() {
        Node* curr = head_;
        while (curr) {
            Node* next = curr->next;
            delete curr;
            curr = next;
        }
    }

    std::optional<Value> get(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end()) {
            misses_++;
            return std::nullopt;
        }
        
        hits_++;
        Node* node = it->second;
        move_to_front(node);
        return node->value;
    }

    void put(const Key& key, Value value) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            Node* node = it->second;
            node->value = std::move(value);
            move_to_front(node);
            return;
        }

        if (size_ == capacity_) {
            evict_lru();
        }

        Node* new_node = new Node(key, std::move(value));
        map_[key] = new_node;
        add_to_front(new_node);
        size_++;
    }

    void evict_to(size_t new_capacity) {
        while (size_ > new_capacity && size_ > 0) {
            evict_lru();
        }
    }

    void resize(size_t new_capacity) {
        capacity_ = new_capacity;
        evict_to(capacity_);
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    
    double hit_rate() const {
        if (hits_ + misses_ == 0) return 0.0;
        return static_cast<double>(hits_) / (hits_ + misses_);
    }
    
    void reset_stats() {
        hits_ = 0;
        misses_ = 0;
    }
    
    uint64_t get_hits() const { return hits_; }
    uint64_t get_misses() const { return misses_; }

private:
    size_t capacity_;
    size_t size_;
    Node* head_;
    Node* tail_;
    std::unordered_map<Key, Node*> map_;
    
    uint64_t hits_;
    uint64_t misses_;

    void move_to_front(Node* node) {
        if (node == head_) return;
        
        // Remove from current pos
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        
        if (node == tail_) tail_ = node->prev;
        
        // Add to front
        node->next = head_;
        node->prev = nullptr;
        if (head_) head_->prev = node;
        head_ = node;
        
        if (!tail_) tail_ = head_;
    }

    void add_to_front(Node* node) {
        node->next = head_;
        node->prev = nullptr;
        if (head_) head_->prev = node;
        head_ = node;
        if (!tail_) tail_ = head_;
    }

    void evict_lru() {
        if (!tail_) return;
        
        Node* to_remove = tail_;
        map_.erase(to_remove->key);
        
        tail_ = tail_->prev;
        if (tail_) {
            tail_->next = nullptr;
        } else {
            head_ = nullptr;
        }
        
        delete to_remove;
        size_--;
    }
};

struct CacheStats {
    size_t size;
    size_t capacity;
    double hit_rate;
    uint64_t hits;
    uint64_t misses;
};

class GovernedCache {
public:
    GovernedCache(size_t max_capacity, const std::string& socket_path = "/tmp/mem_governor.sock", size_t estimated_entry_bytes = 256);
    ~GovernedCache();

    void start();
    void stop();

    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);

    CacheStats get_stats();

private:
    std::unique_ptr<LRUCache<std::string, std::string>> cache_;
    GovernorClient client_;
    size_t estimated_entry_bytes_;
    std::mutex mutex_;
    
    void handle_shrink(uint64_t target_bytes);
    void handle_grow(uint64_t target_bytes);
};
