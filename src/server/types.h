#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum ColType { TYPE_DECIMAL, TYPE_VARCHAR };

inline ColType colTypeFromStr(const std::string &s) {
    if (s == "INT" || s == "DECIMAL") return TYPE_DECIMAL;
    return TYPE_VARCHAR;
}
inline std::string colTypeToStr(ColType t) {
    return t == TYPE_DECIMAL ? "DECIMAL" : "VARCHAR";
}

struct ColDef {
    std::string name;
    ColType     type;
    bool        is_primary = false;
    bool        not_null   = false;
};

struct TableSchema {
    std::string         table_name;
    std::vector<ColDef> cols;
    int                 pk_col = -1;
};

struct Value {
    enum Kind { KIND_DOUBLE, KIND_STRING, KIND_NULL } kind = KIND_NULL;
    double      dval = 0.0;
    std::string sval;

    static Value fromDouble(double d)            { Value v; v.kind=KIND_DOUBLE; v.dval=d; return v; }
    static Value fromString(const std::string &s){ Value v; v.kind=KIND_STRING; v.sval=s; return v; }
    static Value null()                          { return Value{}; }

    bool isNull() const { return kind == KIND_NULL; }

    std::string toString() const {
        if (kind == KIND_NULL)   return "NULL";
        if (kind == KIND_DOUBLE) {
            if (dval == (int64_t)dval) return std::to_string((int64_t)dval);
            return std::to_string(dval);
        }
        return sval;
    }

    bool operator==(const Value &o) const {
        if (kind != o.kind) return false;
        if (kind == KIND_DOUBLE) return dval == o.dval;
        if (kind == KIND_STRING) return sval == o.sval;
        return true;
    }
    bool operator<(const Value &o) const {
        if (kind == KIND_DOUBLE && o.kind == KIND_DOUBLE) return dval < o.dval;
        if (kind == KIND_STRING && o.kind == KIND_STRING) return sval < o.sval;
        return false;
    }
    bool operator<=(const Value &o) const { return *this == o || *this < o; }
    bool operator>(const Value &o)  const { return !(*this <= o); }
    bool operator>=(const Value &o) const { return !(*this < o); }
};

struct Row {
    std::vector<Value> vals;
    bool valid = true;
};
