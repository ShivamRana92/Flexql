#include "executor.h"
#include <cctype>

static std::string toUpper(const std::string &s) {
    std::string r=s; for(auto &c:r) c=(char)toupper((unsigned char)c); return r;
}

Executor::Executor(StorageEngine &storage, LRUCache &cache)
    : storage_(storage), cache_(cache) {}

int Executor::colIdx(const TableSchema &schema, const std::string &name) const {
    std::string up=toUpper(name);
    for (int i=0;i<(int)schema.cols.size();i++)
        if (toUpper(schema.cols[i].name)==up) return i;
    return -1;
}

Value Executor::getVal(const Condition &cond, bool left_side,
                       const Row &rowA, const TableSchema &schA,
                       const Row *rowB, const TableSchema *schB) const
{
    if (left_side) {
        int idx=colIdx(schA,cond.left_col);
        if (idx>=0) return rowA.vals[(size_t)idx];
        if (rowB&&schB) { idx=colIdx(*schB,cond.left_col); if(idx>=0) return rowB->vals[(size_t)idx]; }
        return Value::null();
    }
    if (!cond.has_right_col) return cond.right_val;
    if (!cond.right_table.empty()) {
        std::string rt=toUpper(cond.right_table);
        if (rt==toUpper(schA.table_name)) { int i=colIdx(schA,cond.right_col); if(i>=0) return rowA.vals[(size_t)i]; }
        if (rowB&&schB&&rt==toUpper(schB->table_name)) { int i=colIdx(*schB,cond.right_col); if(i>=0) return rowB->vals[(size_t)i]; }
    }
    int idx=colIdx(schA,cond.right_col);
    if (idx>=0) return rowA.vals[(size_t)idx];
    if (rowB&&schB) { idx=colIdx(*schB,cond.right_col); if(idx>=0) return rowB->vals[(size_t)idx]; }
    return Value::null();
}

bool Executor::evalCond(const Condition &cond,
                        const Row &rowA, const TableSchema &schA,
                        const Row *rowB, const TableSchema *schB) const
{
    Value lv, rv;
    if (!cond.left_table.empty()) {
        std::string lt=toUpper(cond.left_table);
        if (lt==toUpper(schA.table_name)) {
            int i=colIdx(schA,cond.left_col); lv=(i>=0)?rowA.vals[(size_t)i]:Value::null();
        } else if (rowB&&schB&&lt==toUpper(schB->table_name)) {
            int i=colIdx(*schB,cond.left_col); lv=(i>=0)?rowB->vals[(size_t)i]:Value::null();
        } else lv=Value::null();
    } else {
        lv=getVal(cond,true,rowA,schA,rowB,schB);
    }
    rv=getVal(cond,false,rowA,schA,rowB,schB);

    if (cond.op=="=")  return lv==rv;
    if (cond.op=="<")  return lv<rv;
    if (cond.op==">")  return lv>rv;
    if (cond.op=="<=") return lv<=rv;
    if (cond.op==">=") return lv>=rv;
    return false;
}

ExecResult Executor::execCreate(const CreateTableStmt &s) {
    if (storage_.tableExists(s.table_name)) {
        return {true, "", {}};
    }
    TableSchema schema;
    schema.table_name = s.table_name;
    schema.cols       = s.cols;

    std::string err;
    if (!storage_.createTable(schema, err))
        return {false, err, {}};
    return {true, "", {}};
}

ExecResult Executor::execInsert(const InsertStmt &s) {
    Table *tbl=storage_.getTable(s.table_name);
    if (!tbl) return {false,"Table '"+s.table_name+"' does not exist",{}};

    std::vector<Row> rows;
    rows.reserve(s.rows.size());
    for (auto &vals:s.rows) {
        if (vals.size()!=tbl->schema.cols.size())
            return {false,"Column count mismatch: table has "+
                std::to_string(tbl->schema.cols.size())+" columns, got "+
                std::to_string(vals.size()),{}};
        Row r; r.vals=vals; r.valid=true;
        rows.push_back(r);
    }

    std::string err;
    int n=storage_.insertRows(s.table_name,rows,err);
    if (n<0) return {false,err,{}};
    cache_.invalidate(s.table_name);
    return {true,"",{}};
}

ExecResult Executor::execSelect(const SelectStmt &s) {
    Table *tblA=storage_.getTable(s.table_name);
    if (!tblA) return {false,"Table '"+s.table_name+"' does not exist",{}};

    std::string err;
    auto rowsA=storage_.scanAll(s.table_name,err);
    if (!err.empty()) return {false,err,{}};

    std::vector<Row> rowsB_all;
    Table *tblB=nullptr;
    if (s.has_join) {
        tblB=storage_.getTable(s.join_table);
        if (!tblB) return {false,"Table '"+s.join_table+"' does not exist",{}};
        rowsB_all=storage_.scanAll(s.join_table,err);
        if (!err.empty()) return {false,err,{}};
    }

    struct OutCol { std::string name; bool from_b; int idx; };
    std::vector<OutCol> out_cols;

    for (auto &sc:s.cols) {
        if (sc.is_star) {
            for (int i=0;i<(int)tblA->schema.cols.size();i++)
                out_cols.push_back({tblA->schema.cols[i].name,false,i});
            if (tblB)
                for (int i=0;i<(int)tblB->schema.cols.size();i++)
                    out_cols.push_back({tblB->schema.cols[i].name,true,i});
            continue;
        }
        bool in_b=false; int idx=-1;
        if (!sc.table_prefix.empty()) {
            if (toUpper(sc.table_prefix)==toUpper(s.table_name))
                idx=colIdx(tblA->schema,sc.col_name);
            else if (tblB&&toUpper(sc.table_prefix)==toUpper(s.join_table)) {
                idx=colIdx(tblB->schema,sc.col_name); in_b=(idx>=0);
            }
        } else {
            idx=colIdx(tblA->schema,sc.col_name);
            if (idx<0&&tblB) { idx=colIdx(tblB->schema,sc.col_name); in_b=(idx>=0); }
        }
        if (idx<0) return {false,"Unknown column '"+sc.col_name+"'",{}};
        out_cols.push_back({sc.col_name,in_b,idx});
    }

    std::vector<std::string> col_names;
    for (auto &oc:out_cols) col_names.push_back(toUpper(oc.name));

    ExecResult res; res.ok=true;

    auto emit=[&](const Row &ra, const Row *rb) {
        if (s.has_where && !evalCond(s.where_cond,ra,tblA->schema,rb,tblB?&tblB->schema:nullptr))
            return;
        ResultRow rr;
        rr.col_names=col_names;
        for (auto &oc:out_cols) {
            const Row &row=oc.from_b?*rb:ra;
            rr.values.push_back(row.vals[(size_t)oc.idx].toString());
        }
        res.rows.push_back(rr);
    };

    if (!s.has_join) {
        for (auto &ra:rowsA) emit(ra,nullptr);
    } else {
        for (auto &ra:rowsA)
            for (auto &rb:rowsB_all)
                if (evalCond(s.join_cond,ra,tblA->schema,&rb,&tblB->schema))
                    emit(ra,&rb);
    }
    return res;
}

ExecResult Executor::execute(const Statement &stmt) {
    switch (stmt.type) {
        case STMT_CREATE_TABLE: return execCreate(*stmt.create_table);
        case STMT_INSERT:       return execInsert(*stmt.insert);
        case STMT_SELECT:       return execSelect(*stmt.select);
        default:                return {false,"Unsupported statement",{}};
    }
}
