#include "parser.h"
#include <cctype>
#include <algorithm>
#include <cstdlib>

static const char *KEYWORDS[] = {
    "CREATE","TABLE","INSERT","INTO","VALUES",
    "SELECT","FROM","WHERE","INNER","JOIN","ON",
    "PRIMARY","KEY","NOT","NULL","AND","OR",
    "INT","DECIMAL","VARCHAR","TEXT","DATETIME",
    nullptr
};

static bool isKeyword(const std::string &s) {
    std::string up = s;
    for (auto &c : up) c = (char)toupper((unsigned char)c);
    for (int i = 0; KEYWORDS[i]; ++i)
        if (up == KEYWORDS[i]) return true;
    return false;
}

static std::string toUpper(const std::string &s) {
    std::string r = s;
    for (auto &c : r) c = (char)toupper((unsigned char)c);
    return r;
}

void Parser::skipWs() {
    while (pos_ < src_.size() && isspace((unsigned char)src_[pos_])) ++pos_;
}

Parser::Token Parser::nextRaw() {
    skipWs();
    if (pos_ >= src_.size()) return {Token::END_OF_INPUT, ""};
    char c = src_[pos_];

    if (pos_+1 < src_.size()) {
        char c2 = src_[pos_+1];
        if (c=='<' && c2=='=') { pos_+=2; return {Token::LE, "<="}; }
        if (c=='>' && c2=='=') { pos_+=2; return {Token::GE, ">="}; }
    }

    if (c=='(') { ++pos_; return {Token::LPAREN,    "("}; }
    if (c==')') { ++pos_; return {Token::RPAREN,    ")"}; }
    if (c==',') { ++pos_; return {Token::COMMA,     ","}; }
    if (c==';') { ++pos_; return {Token::SEMICOLON, ";"}; }
    if (c=='.') { ++pos_; return {Token::DOT,       "."}; }
    if (c=='=') { ++pos_; return {Token::EQ,        "="}; }
    if (c=='<') { ++pos_; return {Token::LT,        "<"}; }
    if (c=='>') { ++pos_; return {Token::GT,        ">"}; }
    if (c=='*') { ++pos_; return {Token::STAR,      "*"}; }

    if (c == '\'') {
        ++pos_;
        std::string s;
        while (pos_ < src_.size() && src_[pos_] != '\'') {
            if (src_[pos_] == '\\' && pos_+1 < src_.size()) { ++pos_; s += src_[pos_++]; }
            else s += src_[pos_++];
        }
        if (pos_ < src_.size()) ++pos_;
        return {Token::STRING, s};
    }

    if (isdigit((unsigned char)c) || (c=='-' && pos_+1<src_.size() && isdigit((unsigned char)src_[pos_+1]))) {
        std::string s;
        if (c=='-') { s += c; ++pos_; }
        while (pos_ < src_.size() && (isdigit((unsigned char)src_[pos_]) || src_[pos_]=='.'))
            s += src_[pos_++];
        return {Token::NUMBER, s};
    }

    if (isalpha((unsigned char)c) || c=='_') {
        std::string s;
        while (pos_ < src_.size() && (isalnum((unsigned char)src_[pos_]) || src_[pos_]=='_'))
            s += src_[pos_++];
        std::string up = toUpper(s);
        if (isKeyword(up)) return {Token::KW, up};
        return {Token::IDENT, s};
    }

    ++pos_; return nextRaw();
}

Parser::Token Parser::next() {
    if (peek_valid_) { cur_ = peek_; peek_valid_ = false; }
    else cur_ = nextRaw();
    return cur_;
}

Parser::Token Parser::peek() {
    if (!peek_valid_) { peek_ = nextRaw(); peek_valid_ = true; }
    return peek_;
}

void Parser::consume() { next(); }

bool Parser::expect(Token::Type t, const std::string &val) {
    Token tok = next();
    if (tok.type != t) return false;
    if (!val.empty() && tok.val != val) return false;
    return true;
}

bool Parser::matchKw(const std::string &kw) {
    Token tok = next();
    return tok.type == Token::KW && tok.val == kw;
}

bool Parser::peekKw(const std::string &kw) {
    Token tok = peek();
    return tok.type == Token::KW && tok.val == kw;
}

bool Parser::parseValue(Value &v, std::string &err) {
    Token tok = next();
    if (tok.type == Token::NUMBER) { v = Value::fromDouble(std::stod(tok.val)); return true; }
    if (tok.type == Token::STRING) { v = Value::fromString(tok.val); return true; }
    if (tok.type == Token::KW && tok.val == "NULL") { v = Value::null(); return true; }
    err = "Expected value, got '" + tok.val + "'";
    return false;
}

bool Parser::parseCondition(Condition &c, std::string &err) {
    Token left = next();
    if (left.type != Token::IDENT && left.type != Token::KW) {
        err = "Expected column name in condition"; return false;
    }

    if (peek().type == Token::DOT) {
        consume();
        c.left_table = left.val;
        Token col = next();
        if (col.type != Token::IDENT && col.type != Token::KW) {
            err = "Expected column after '.'"; return false;
        }
        c.left_col = toUpper(col.val);
    } else {
        c.left_table.clear();
        c.left_col = toUpper(left.val);
    }

    Token op = next();
    if      (op.type == Token::EQ) c.op = "=";
    else if (op.type == Token::LT) c.op = "<";
    else if (op.type == Token::GT) c.op = ">";
    else if (op.type == Token::LE) c.op = "<=";
    else if (op.type == Token::GE) c.op = ">=";
    else { err = "Expected operator"; return false; }

    Token right = next();
    if ((right.type == Token::IDENT || right.type == Token::KW) && peek().type == Token::DOT) {
        consume();
        Token rcol = next();
        if (rcol.type != Token::IDENT && rcol.type != Token::KW) {
            err = "Expected column after '.'"; return false;
        }
        c.has_right_col = true;
        c.right_table   = right.val;
        c.right_col     = toUpper(rcol.val);
    } else if (right.type == Token::IDENT || (right.type == Token::KW && right.val != "NULL")) {
        c.has_right_col = true;
        c.right_table.clear();
        c.right_col = toUpper(right.val);
    } else if (right.type == Token::NUMBER) {
        c.has_right_col = false;
        c.right_val = Value::fromDouble(std::stod(right.val));
    } else if (right.type == Token::STRING) {
        c.has_right_col = false;
        c.right_val = Value::fromString(right.val);
    } else {
        err = "Expected value or column in condition"; return false;
    }
    return true;
}

bool Parser::parseCreate(Statement &out, std::string &err) {
    if (!matchKw("TABLE")) { err = "Expected TABLE"; return false; }
    Token name = next();
    if (name.type != Token::IDENT && name.type != Token::KW) { err = "Expected table name"; return false; }

    auto stmt = std::make_shared<CreateTableStmt>();
    stmt->table_name = toUpper(name.val);
    if (!expect(Token::LPAREN)) { err = "Expected '('"; return false; }

    while (true) {
        Token col = next();
        if (col.type == Token::RPAREN) break;
        if (col.type != Token::IDENT && col.type != Token::KW) { err = "Expected column name"; return false; }

        ColDef cd;
        cd.name = toUpper(col.val);
        Token type = next();
        if (type.type != Token::KW && type.type != Token::IDENT) { err = "Expected type"; return false; }
        cd.type = colTypeFromStr(toUpper(type.val));

        if (peek().type == Token::LPAREN) { consume(); next(); expect(Token::RPAREN); }

        while (true) {
            Token mod = peek();
            if (mod.type == Token::KW && mod.val == "PRIMARY") {
                consume();
                if (!matchKw("KEY")) { err = "Expected KEY"; return false; }
                cd.is_primary = true;
            } else if (mod.type == Token::KW && mod.val == "NOT") {
                consume();
                if (!matchKw("NULL")) { err = "Expected NULL"; return false; }
                cd.not_null = true;
            } else break;
        }

        stmt->cols.push_back(cd);
        Token sep = peek();
        if (sep.type == Token::COMMA)  { consume(); continue; }
        if (sep.type == Token::RPAREN) { consume(); break; }
        err = "Expected ',' or ')'"; return false;
    }

    if (peek().type == Token::SEMICOLON) consume();
    out.type = STMT_CREATE_TABLE;
    out.create_table = stmt;
    return true;
}

bool Parser::parseInsert(Statement &out, std::string &err) {
    if (!matchKw("INTO")) { err = "Expected INTO"; return false; }
    Token name = next();
    if (name.type != Token::IDENT && name.type != Token::KW) { err = "Expected table name"; return false; }

    auto stmt = std::make_shared<InsertStmt>();
    stmt->table_name = toUpper(name.val);
    if (!matchKw("VALUES")) { err = "Expected VALUES"; return false; }

    while (true) {
        if (!expect(Token::LPAREN)) { err = "Expected '('"; return false; }
        std::vector<Value> row;
        while (true) {
            if (peek().type == Token::RPAREN) { consume(); break; }
            Value v;
            if (!parseValue(v, err)) return false;
            row.push_back(v);
            if (peek().type == Token::COMMA)  { consume(); continue; }
            if (peek().type == Token::RPAREN) { consume(); break; }
            err = "Expected ',' or ')'"; return false;
        }
        stmt->rows.push_back(row);
        if (peek().type == Token::COMMA) { consume(); continue; }
        break;
    }

    if (peek().type == Token::SEMICOLON) consume();
    out.type   = STMT_INSERT;
    out.insert = stmt;
    return true;
}

bool Parser::parseSelect(Statement &out, std::string &err) {
    auto stmt = std::make_shared<SelectStmt>();

    while (true) {
        Token t = peek();
        if (t.type == Token::STAR) {
            consume();
            SelectCol sc; sc.is_star = true;
            stmt->cols.push_back(sc);
        } else if (t.type == Token::IDENT || t.type == Token::KW) {
            consume();
            SelectCol sc;
            if (peek().type == Token::DOT) {
                consume();
                sc.table_prefix = t.val;
                Token col = next();
                sc.col_name = toUpper(col.val);
            } else {
                sc.col_name = toUpper(t.val);
            }
            stmt->cols.push_back(sc);
        } else { err = "Expected column list"; return false; }

        if (peek().type == Token::COMMA) { consume(); continue; }
        break;
    }

    if (!matchKw("FROM")) { err = "Expected FROM"; return false; }
    Token tbl = next();
    if (tbl.type != Token::IDENT && tbl.type != Token::KW) { err = "Expected table name"; return false; }
    stmt->table_name = toUpper(tbl.val);

    if (peekKw("INNER")) {
        consume();
        if (!matchKw("JOIN")) { err = "Expected JOIN"; return false; }
        Token jtbl = next();
        if (jtbl.type != Token::IDENT && jtbl.type != Token::KW) { err = "Expected join table"; return false; }
        stmt->join_table = toUpper(jtbl.val);
        if (!matchKw("ON")) { err = "Expected ON"; return false; }
        if (!parseCondition(stmt->join_cond, err)) return false;
        stmt->has_join = true;
    }

    if (peekKw("WHERE")) {
        consume();
        if (!parseCondition(stmt->where_cond, err)) return false;
        stmt->has_where = true;
    }

    if (peek().type == Token::SEMICOLON) consume();
    out.type   = STMT_SELECT;
    out.select = stmt;
    return true;
}

bool Parser::parse(const std::string &sql, Statement &out, std::string &err) {
    src_ = sql; pos_ = 0; peek_valid_ = false;
    Token first = next();
    if (first.type == Token::END_OF_INPUT) { err = "Empty query"; return false; }
    if (first.type != Token::KW) { err = "Expected SQL keyword, got '" + first.val + "'"; return false; }
    if (first.val == "CREATE") return parseCreate(out, err);
    if (first.val == "INSERT") return parseInsert(out, err);
    if (first.val == "SELECT") return parseSelect(out, err);
    err = "Unknown statement: " + first.val;
    return false;
}
