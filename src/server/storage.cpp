#include "storage.h"
#include <fstream>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>

static void writeU8(std::vector<char> &b, uint8_t v)  { b.push_back((char)v); }
static void writeU16(std::vector<char> &b, uint16_t v){ b.push_back(v&0xff); b.push_back((v>>8)&0xff); }
static void writeDouble(std::vector<char> &b, double v){ char t[8]; memcpy(t,&v,8); for(int i=0;i<8;i++) b.push_back(t[i]); }
static void writeStr(std::vector<char> &b, const std::string &s){
    uint16_t len=(uint16_t)std::min(s.size(),(size_t)0xFFFF);
    writeU16(b,len); for(size_t i=0;i<len;i++) b.push_back(s[i]);
}

static bool readU8(const char *b, size_t bl, size_t &o, uint8_t &v) {
    if (o+1 > bl) return false;
    v = (uint8_t)b[o++];
    return true;
}
static bool readU16(const char *b, size_t bl, size_t &o, uint16_t &v) {
    if (o+2 > bl) return false;
    v = (uint8_t)b[o] | ((uint8_t)b[o+1] << 8);
    o += 2;
    return true;
}
static bool readDouble(const char *b, size_t bl, size_t &o, double &v) {
    if (o+8 > bl) return false;
    memcpy(&v, b+o, 8);
    o += 8;
    return true;
}
static bool readStr(const char *b, size_t bl, size_t &o, std::string &s) {
    uint16_t len;
    if (!readU16(b, bl, o, len)) return false;
    if (o+len > bl) return false;
    s.assign(b+o, len);
    o += len;
    return true;
}

static const char DATA_MAGIC[8] = {'F','L','X','D','A','T','1','0'};

StorageEngine::StorageEngine(const std::string &data_dir) : data_dir_(data_dir) {
    mkdir(data_dir_.c_str(), 0755);
}

std::string StorageEngine::schemaPath(const std::string &n) const { return data_dir_+"/"+n+".schema"; }
std::string StorageEngine::dataPath  (const std::string &n) const { return data_dir_+"/"+n+".dat"; }

void StorageEngine::saveSchema(const Table &tbl) {
    std::ofstream f(schemaPath(tbl.schema.table_name));
    f << "NUM_COLS " << tbl.schema.cols.size() << "\n";
    for (auto &c : tbl.schema.cols)
        f << "COL " << c.name << " " << colTypeToStr(c.type)
          << " " << (c.is_primary?1:0) << " " << (c.not_null?1:0) << "\n";
}

static bool loadSchema(const std::string &path, TableSchema &schema) {
    std::ifstream f(path);
    if (!f) return false;
    std::string tag; int n=0;
    f >> tag >> n;
    if (tag != "NUM_COLS") return false;
    int pk_col = -1;
    for (int i = 0; i < n; ++i) {
        std::string ctag,cname,ctype; int ispk,isnn;
        f >> ctag >> cname >> ctype >> ispk >> isnn;
        ColDef cd;
        cd.name=cname; cd.type=colTypeFromStr(ctype);
        cd.is_primary=ispk!=0; cd.not_null=isnn!=0;
        if (cd.is_primary && pk_col<0) pk_col=i;
        schema.cols.push_back(cd);
    }
    schema.pk_col=pk_col;
    return true;
}

std::vector<char> StorageEngine::serializeRow(const Row &row, const TableSchema &schema) {
    std::vector<char> buf;
    writeU8(buf, row.valid?1:0);
    for (size_t i=0;i<schema.cols.size();i++) {
        const Value &v=(i<row.vals.size())?row.vals[i]:Value::null();
        if (schema.cols[i].type==TYPE_DECIMAL)
            writeDouble(buf, v.kind==Value::KIND_DOUBLE?v.dval:0.0);
        else
            writeStr(buf, v.kind==Value::KIND_STRING?v.sval:"");
    }
    return buf;
}

bool StorageEngine::deserializeRow(const char *buf, size_t len, Row &row, const TableSchema &schema) {
    size_t off=0;
    uint8_t valid; if(!readU8(buf,len,off,valid))return false;
    row.valid=(valid!=0);
    row.vals.resize(schema.cols.size());
    for (size_t i=0;i<schema.cols.size();i++) {
        if (schema.cols[i].type==TYPE_DECIMAL) {
            double d; if(!readDouble(buf,len,off,d))return false;
            row.vals[i]=Value::fromDouble(d);
        } else {
            std::string s; if(!readStr(buf,len,off,s))return false;
            row.vals[i]=Value::fromString(s);
        }
    }
    return true;
}

bool StorageEngine::loadTable(const std::string &name) {
    auto tbl=std::make_unique<Table>();
    tbl->schema.table_name=name;
    if (!loadSchema(schemaPath(name), tbl->schema)) return false;

    std::ifstream f(dataPath(name), std::ios::binary);
    if (f) {
        char magic[8]; f.read(magic,8);
        if (memcmp(magic,DATA_MAGIC,8)==0) {
            while (f) {
                uint32_t rlen=0;
                f.read((char*)&rlen,4);
                if (!f||rlen==0) break;
                std::vector<char> buf(rlen);
                f.read(buf.data(),rlen);
                if (!f) break;
                Row row;
                if (deserializeRow(buf.data(),rlen,row,tbl->schema)) {
                    if (row.valid) tbl->live_rows++;
                    tbl->rows.push_back(std::move(row));
                }
            }
        }
    }
    rebuildIndex(*tbl);
    tables_[name]=std::move(tbl);
    return true;
}

void StorageEngine::loadAll() {
    DIR *d=opendir(data_dir_.c_str());
    if (!d) return;
    struct dirent *e;
    std::vector<std::string> names;
    while ((e=readdir(d))!=nullptr) {
        std::string fn=e->d_name;
        if (fn.size()>7 && fn.substr(fn.size()-7)==".schema")
            names.push_back(fn.substr(0,fn.size()-7));
    }
    closedir(d);
    for (auto &n:names) loadTable(n);
}

void StorageEngine::rebuildIndex(Table &tbl) {
    tbl.pk_index.clear();
    if (tbl.schema.pk_col<0) return;
    for (size_t i=0;i<tbl.rows.size();i++) {
        if (!tbl.rows[i].valid) continue;
        const Value &pk=tbl.rows[i].vals[(size_t)tbl.schema.pk_col];
        tbl.pk_index[pk.toString()].push_back(i);
    }
}

bool StorageEngine::tableExists(const std::string &name) const { return tables_.count(name)>0; }

Table *StorageEngine::getTable(const std::string &name) {
    auto it=tables_.find(name);
    return it==tables_.end()?nullptr:it->second.get();
}

bool StorageEngine::createTable(const TableSchema &schema, std::string &err) {
    if (tableExists(schema.table_name)) {
        err="Table '"+schema.table_name+"' already exists"; return false;
    }
    auto tbl=std::make_unique<Table>();
    tbl->schema=schema;
    tbl->schema.pk_col=-1;
    for (int i=0;i<(int)schema.cols.size();i++)
        if (schema.cols[i].is_primary) { tbl->schema.pk_col=i; break; }
    if (tbl->schema.pk_col<0 && !schema.cols.empty()) tbl->schema.pk_col=0;

    saveSchema(*tbl);
    { std::ofstream f(dataPath(schema.table_name),std::ios::binary); f.write(DATA_MAGIC,8); }
    tables_[schema.table_name]=std::move(tbl);
    return true;
}

int StorageEngine::insertRows(const std::string &table_name,
                               const std::vector<Row> &rows, std::string &err) {
    auto it=tables_.find(table_name);
    if (it==tables_.end()) { err="Table '"+table_name+"' does not exist"; return -1; }
    Table &tbl=*it->second;

    std::ofstream f(dataPath(table_name), std::ios::binary|std::ios::app);
    if (!f) { err="Cannot open data file"; return -1; }
    f.rdbuf()->pubsetbuf(nullptr, 1<<20);

    int inserted=0;
    for (auto &row:rows) {
        if (row.vals.size()!=tbl.schema.cols.size()) {
            err="Column count mismatch: expected "+std::to_string(tbl.schema.cols.size())+
                " got "+std::to_string(row.vals.size());
            return -1;
        }
        size_t idx=tbl.rows.size();
        tbl.rows.push_back(row);
        tbl.live_rows++;
        if (tbl.schema.pk_col>=0) {
            const Value &pk=row.vals[(size_t)tbl.schema.pk_col];
            tbl.pk_index[pk.toString()].push_back(idx);
        }
        auto buf=serializeRow(row,tbl.schema);
        uint32_t rlen=(uint32_t)buf.size();
        f.write((char*)&rlen,4);
        f.write(buf.data(),buf.size());
        ++inserted;
    }
    return inserted;
}

std::vector<Row> StorageEngine::scanAll(const std::string &table_name, std::string &err) {
    auto it=tables_.find(table_name);
    if (it==tables_.end()) { err="Table '"+table_name+"' does not exist"; return {}; }
    std::vector<Row> result;
    for (auto &row:it->second->rows)
        if (row.valid) result.push_back(row);
    return result;
}

std::vector<size_t> StorageEngine::pkLookup(const std::string &table_name, const std::string &pk_str) {
    auto it=tables_.find(table_name);
    if (it==tables_.end()) return {};
    auto jt=it->second->pk_index.find(pk_str);
    if (jt==it->second->pk_index.end()) return {};
    return jt->second;
}
