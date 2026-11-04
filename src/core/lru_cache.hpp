#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace streetpano::core {

// Keeps recently used items within a fixed number of bytes, dropping the oldest first.
// This class isn't thread-safe. One thread owns each cache, and other threads pass it
// results by message.
template <typename Key, typename Value>
class LruCache {
  public:
    explicit LruCache(std::size_t budgetBytes) : budgetBytes_(budgetBytes) {}

    // Returns false if the item can never fit. The cache then holds no copy of that key.
    bool put(const Key& key, Value value, std::size_t bytes) {
        erase(key);
        if (bytes > budgetBytes_) {
            return false;
        }
        usedBytes_ += bytes;
        order_.push_front(Entry{key, std::move(value), bytes});
        index_.emplace(key, order_.begin());
        while (usedBytes_ > budgetBytes_) {
            eraseEntry(std::prev(order_.end()));
        }
        return true;
    }

    // Returns a copy and marks the item as recently used.
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        const auto found = index_.find(key);
        if (found == index_.end()) {
            return std::nullopt;
        }
        order_.splice(order_.begin(), order_, found->second);
        return found->second->value;
    }

    [[nodiscard]] bool contains(const Key& key) const { return index_.count(key) > 0; }

    void erase(const Key& key) {
        const auto found = index_.find(key);
        if (found != index_.end()) {
            eraseEntry(found->second);
        }
    }

    void clear() {
        index_.clear();
        order_.clear();
        usedBytes_ = 0;
    }

    [[nodiscard]] std::size_t usedBytes() const { return usedBytes_; }
    [[nodiscard]] std::size_t budgetBytes() const { return budgetBytes_; }
    [[nodiscard]] std::size_t size() const { return order_.size(); }

  private:
    struct Entry {
        Key key;
        Value value;
        std::size_t bytes;
    };
    using Iterator = typename std::list<Entry>::iterator;

    void eraseEntry(Iterator entry) {
        usedBytes_ -= entry->bytes;
        index_.erase(entry->key);
        order_.erase(entry);
    }

    std::size_t budgetBytes_;
    std::size_t usedBytes_ = 0;
    std::list<Entry> order_;
    std::unordered_map<Key, Iterator> index_;
};

}  // namespace streetpano::core
