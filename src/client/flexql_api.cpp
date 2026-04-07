#include "../../include/flexql.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

struct FlexQL { int sock; };

static bool parseLenToken(const std::string &text, size_t &pos, std::string &out) {
    size_t colon = text.find(':', pos);
    if (colon == std::string::npos || colon == pos) return false;
    for (size_t i = pos; i < colon; ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    size_t len = std::stoul(text.substr(pos, colon - pos));
    size_t start = colon + 1;
    if (start + len > text.size()) return false;
    out.assign(text, start, len);
    pos = start + len;
    return true;
}

static bool parseRowPayload(const std::string &payload,
                             std::vector<std::string> &values,
                             std::vector<std::string> &colNames)
{
    values.clear(); colNames.clear();
    if (payload.empty()) return false;
    size_t countEnd = payload.find(' ');
    if (countEnd == std::string::npos || countEnd == 0) return false;
    int colCount = 0;
    try { colCount = std::stoi(payload.substr(0, countEnd)); } catch (...) { return false; }
    if (colCount < 0) return false;
    size_t pos = countEnd + 1;
    for (int i = 0; i < colCount; ++i) {
        std::string name, val;
        if (!parseLenToken(payload, pos, name)) return false;
        if (!parseLenToken(payload, pos, val))  return false;
        colNames.push_back(name);
        values.push_back(val);
    }
    return true;
}

int flexql_open(const char *host, int port, FlexQL **outDb) {
    if (!host || !outDb) return FLEXQL_ERROR;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return FLEXQL_ERROR;
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { close(sock); return FLEXQL_ERROR; }
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(sock); return FLEXQL_ERROR; }
    FlexQL *db = (FlexQL*)malloc(sizeof(FlexQL));
    if (!db) { close(sock); return FLEXQL_ERROR; }
    db->sock = sock;
    *outDb   = db;
    return FLEXQL_OK;
}

int flexql_close(FlexQL *db) {
    if (!db) return FLEXQL_ERROR;
    close(db->sock);
    free(db);
    return FLEXQL_OK;
}

int flexql_exec(FlexQL *db, const char *sql,
                int (*callback)(void*, int, char**, char**),
                void *arg, char **errmsg)
{
    if (!db || !sql) {
        if (errmsg) { const char *m="Invalid args"; *errmsg=(char*)malloc(strlen(m)+1); if(*errmsg) strcpy(*errmsg,m); }
        return FLEXQL_ERROR;
    }
    if (send(db->sock, sql, strlen(sql), MSG_NOSIGNAL) < 0) {
        if (errmsg) { const char *m="send failed"; *errmsg=(char*)malloc(strlen(m)+1); if(*errmsg) strcpy(*errmsg,m); }
        return FLEXQL_ERROR;
    }

    std::string pending;
    char buf[4096];
    ssize_t n;
    bool done=false, hasError=false;
    std::string errorText;

    while (!done && (n=read(db->sock,buf,sizeof(buf)-1)) > 0) {
        buf[n]='\0';
        pending.append(buf,(size_t)n);
        size_t nlPos;
        while ((nlPos=pending.find('\n')) != std::string::npos) {
            std::string line=pending.substr(0,nlPos);
            pending.erase(0,nlPos+1);
            if (line=="END") { done=true; break; }
            if (line.rfind("ERROR:",0)==0) {
                hasError=true;
                errorText=line.substr(6);
                size_t sp=errorText.find_first_not_of(' ');
                if (sp!=std::string::npos) errorText=errorText.substr(sp);
                continue;
            }
            if (line=="OK") continue;
            if (callback && line.rfind("ROW ",0)==0) {
                std::string payload=line.substr(4);
                std::vector<std::string> values,colNames;
                if (parseRowPayload(payload,values,colNames)) {
                    std::vector<char*> av(values.size()),cv(colNames.size());
                    for (size_t i=0;i<values.size();i++) { av[i]=(char*)values[i].c_str(); cv[i]=(char*)colNames[i].c_str(); }
                    if (callback(arg,(int)av.size(),av.data(),cv.data())!=0) { done=true; break; }
                } else {
                    char *a0=(char*)payload.c_str(), *c0=(char*)"row";
                    callback(arg,1,&a0,&c0);
                }
            }
        }
    }

    if (!done) {
        if (errmsg) { const char *m="Connection closed before END"; *errmsg=(char*)malloc(strlen(m)+1); if(*errmsg) strcpy(*errmsg,m); }
        return FLEXQL_ERROR;
    }
    if (hasError) {
        if (errmsg) { *errmsg=(char*)malloc(errorText.size()+1); if(*errmsg) strcpy(*errmsg,errorText.c_str()); }
        return FLEXQL_ERROR;
    }
    return FLEXQL_OK;
}

void flexql_free(void *ptr) { free(ptr); }
