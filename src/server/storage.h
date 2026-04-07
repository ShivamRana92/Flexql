#pragma once
#include "types.h"
#include "cache.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

struct Table {
    TableSchema  schema;
    std::vector<Row> rows;
    std::unordered_map<std::string, std::vector<size_t>> pk_index;
    size_t live_rows = 0;
};

class StorageEngine {
public:
    explicit StorageEngine(const std::string &data_dir);
    void loadAll();

    bool   createTable(const TableSchema &schema, std::string &err);
    bool   tableExists(const std::string &name) const;
    Table *getTable(const std::string &name);

    int  insertRows(const std::string &table_name,
                    const std::vector<Row> &rows,
                    std::string &err);

    std::vector<Row> scanAll(const std::string &table_name, std::string &err);
    std::vector<size_t> pkLookup(const std::string &table_name, const std::string &pk_str);

private:
    std::string data_dir_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;

    std::string schemaPath(const std::string &name) const;
    std::string dataPath  (const std::string &name) const;

    bool loadTable(const std::string &name);
    void saveSchema(const Table &tbl);

    std::vector<char> serializeRow(const Row &row, const TableSchema &schema);
    bool deserializeRow(const char *buf, size_t len, Row &row, const TableSchema &schema);
    void rebuildIndex(Table &tbl);
};
