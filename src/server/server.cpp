/*
 * FlexQL Server — persistent, single-threaded TCP server
 *
 * Wire protocol:
 *   Client → Server : SQL text ending with ';'
 *   Server → Client :
 *     OK\nEND\n                            (non-SELECT success)
 *     ROW <n> <nl>:<name><vl>:<val>...\n  (one line per row)
 *     END\n
 *     ERROR: <msg>\nEND\n                 (on failure)
 */

#include "parser.h"
#include "storage.h"
#include "cache.h"
#include "executor.h"

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <csignal>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static const int   PORT     = 9000;
static const int   BACKLOG  = 16;
static const int   BUFSIZE  = 65536;
static const char *DATA_DIR = "data/tables";

/* ---- helpers ---- */
static std::string formatRow(const ResultRow &row) {
    std::string s = "ROW ";
    s += std::to_string(row.col_names.size());
    s += " ";
    for (size_t i = 0; i < row.col_names.size(); ++i) {
        const std::string &name = row.col_names[i];
        const std::string &val  = row.values[i];
        s += std::to_string(name.size()) + ":" + name;
        s += std::to_string(val.size())  + ":" + val;
    }
    s += "\n";
    return s;
}

static bool sendAll(int sock, const std::string &data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(sock, data.c_str() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

static void handleQuery(const std::string &sql, int client_sock,
                        Parser &parser, Executor &exec)
{
    /* Skip blank / whitespace-only statements */
    if (sql.find_first_not_of(" \t\r\n;") == std::string::npos) {
        sendAll(client_sock, "OK\nEND\n");
        return;
    }

    Statement stmt;
    std::string err;
    if (!parser.parse(sql, stmt, err)) {
        sendAll(client_sock, "ERROR: " + err + "\nEND\n");
        return;
    }

    ExecResult res = exec.execute(stmt);
    if (!res.ok) {
        sendAll(client_sock, "ERROR: " + res.error + "\nEND\n");
        return;
    }

    if (stmt.type != STMT_SELECT) {
        sendAll(client_sock, "OK\nEND\n");
        return;
    }

    std::string resp;
    resp.reserve(res.rows.size() * 64);
    for (auto &row : res.rows) resp += formatRow(row);
    resp += "END\n";
    sendAll(client_sock, resp);
}

static void handleClient(int client_sock, Executor &exec) {
    Parser      parser;
    char        buf[BUFSIZE];
    std::string pending;

    while (true) {
        ssize_t n = recv(client_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        pending.append(buf, (size_t)n);

        size_t pos;
        while ((pos = pending.find(';')) != std::string::npos) {
            std::string sql = pending.substr(0, pos + 1);
            pending.erase(0, pos + 1);
            handleQuery(sql, client_sock, parser, exec);
        }
    }
    close(client_sock);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGPIPE, SIG_IGN);

    /*
     * NO wipe on startup — data in data/tables/ is persistent.
     * Tables created in previous runs are loaded back into memory.
     * CREATE TABLE is idempotent (silently skips if table exists).
     */
    StorageEngine storage(DATA_DIR);
    storage.loadAll();

    LRUCache cache(512);
    Executor exec(storage, cache);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(server_fd, BACKLOG) < 0) { perror("listen"); return 1; }

    std::cout << "FlexQL Server running on port " << PORT
              << "  |  data dir: " << DATA_DIR
              << "  |  persistent storage: ON\n";
    std::cout.flush();

    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_sock < 0) { perror("accept"); continue; }

        std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr) << "\n";
        std::cout.flush();

        handleClient(client_sock, exec);

        std::cout << "Client disconnected\n";
        std::cout.flush();
    }

    close(server_fd);
    return 0;
}
