#include "cache.h"
#include <cctype>

LRUCache::LRUCache(size_t capacity) : cap_(capacity) {}

bool LRUCache::get(const std::string &key, std::vector<ResultRow> &rows) {
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    lru_.splice(lru_.begin(), lru_, it->second);
    rows = it->second->rows;
    return true;
}

void LRUCache::put(const std::string &key, const std::vector<ResultRow> &rows) {
    auto it = map_.find(key);
    if (it != map_.end()) {
        it->second->rows = rows;
        lru_.splice(lru_.begin(), lru_, it->second);
        return;
    }
    if (map_.size() >= cap_) {
        map_.erase(lru_.back().key);
        lru_.pop_back();
    }
    lru_.push_front({key, rows});
    map_[key] = lru_.begin();
}

void LRUCache::invalidate(const std::string &table) {
    std::string up = table;
    for (auto &c : up) c = (char)toupper((unsigned char)c);

    std::vector<std::string> to_remove;
    for (auto &e : lru_) {
        std::string k = e.key;
        for (auto &c : k) c = (char)toupper((unsigned char)c);
        if (k.find(up) != std::string::npos)
            to_remove.push_back(e.key);
    }
    for (auto &k : to_remove) {
        auto it = map_.find(k);
        if (it != map_.end()) { lru_.erase(it->second); map_.erase(it); }
    }
}

void LRUCache::clear() { lru_.clear(); map_.clear(); }
