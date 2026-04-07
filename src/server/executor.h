#pragma once
#include "parser.h"
#include "storage.h"
#include "cache.h"
#include <string>
#include <vector>

struct ExecResult {
    bool                   ok = true;
    std::string            error;
    std::vector<ResultRow> rows;
};

class Executor {
public:
    Executor(StorageEngine &storage, LRUCache &cache);
    ExecResult execute(const Statement &stmt);

private:
    StorageEngine &storage_;
    LRUCache      &cache_;

    ExecResult execCreate(const CreateTableStmt &s);
    ExecResult execInsert(const InsertStmt &s);
    ExecResult execSelect(const SelectStmt &s);

    bool  evalCond(const Condition &cond,
                   const Row &rowA, const TableSchema &schA,
                   const Row *rowB, const TableSchema *schB) const;
    int   colIdx(const TableSchema &schema, const std::string &name) const;
    Value getVal(const Condition &cond, bool left_side,
                 const Row &rowA, const TableSchema &schA,
                 const Row *rowB, const TableSchema *schB) const;
};
