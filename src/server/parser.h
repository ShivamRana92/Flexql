#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <memory>

enum StmtType { STMT_CREATE_TABLE, STMT_INSERT, STMT_SELECT };

struct Condition {
    std::string left_table, left_col;
    std::string op;
    std::string right_table, right_col;
    Value       right_val;
    bool        has_right_col = false;
};

struct SelectCol {
    bool        is_star = false;
    std::string table_prefix;
    std::string col_name;
};

struct CreateTableStmt {
    std::string           table_name;
    std::vector<ColDef>   cols;
};

struct InsertStmt {
    std::string                       table_name;
    std::vector<std::vector<Value>>   rows;
};

struct SelectStmt {
    std::vector<SelectCol> cols;
    std::string            table_name;
    bool        has_join  = false;
    std::string join_table;
    Condition   join_cond;
    bool        has_where = false;
    Condition   where_cond;
};

struct Statement {
    StmtType type;
    std::shared_ptr<CreateTableStmt> create_table;
    std::shared_ptr<InsertStmt>      insert;
    std::shared_ptr<SelectStmt>      select;
};

class Parser {
public:
    bool parse(const std::string &sql, Statement &out, std::string &err);

private:
    std::string src_;
    size_t      pos_ = 0;

    struct Token {
        enum Type {
            KW, IDENT, NUMBER, STRING,
            LPAREN, RPAREN, COMMA, SEMICOLON, DOT,
            EQ, LT, GT, LE, GE, STAR, END_OF_INPUT
        } type;
        std::string val;
    };

    Token cur_, peek_;
    bool  peek_valid_ = false;

    void  skipWs();
    Token nextRaw();
    Token next();
    Token peek();
    void  consume();
    bool  expect(Token::Type t, const std::string &val = "");
    bool  matchKw(const std::string &kw);
    bool  peekKw(const std::string &kw);

    bool parseCreate(Statement &out, std::string &err);
    bool parseInsert(Statement &out, std::string &err);
    bool parseSelect(Statement &out, std::string &err);
    bool parseCondition(Condition &c, std::string &err);
    bool parseValue(Value &v, std::string &err);
};
