#pragma once
#include <string>
#include <list>
#include <unordered_map>
#include <vector>

struct ResultRow {
    std::vector<std::string> col_names;
    std::vector<std::string> values;
};

class LRUCache {
public:
    explicit LRUCache(size_t capacity = 512);
    bool get(const std::string &key, std::vector<ResultRow> &rows);
    void put(const std::string &key, const std::vector<ResultRow> &rows);
    void invalidate(const std::string &table);
    void clear();

private:
    struct Entry {
        std::string            key;
        std::vector<ResultRow> rows;
    };
    size_t cap_;
    std::list<Entry> lru_;
    std::unordered_map<std::string, std::list<Entry>::iterator> map_;
};
