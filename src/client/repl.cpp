#include "../../include/flexql.h"
#include <iostream>
#include <string>
#include <cstdlib>

static int printRow(void*, int cols, char **vals, char **names) {
    for (int i = 0; i < cols; ++i)
        std::cout << (names&&names[i]?names[i]:"?") << " = "
                  << (vals&&vals[i]?vals[i]:"NULL") << "\n";
    std::cout << "\n";
    return 0;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    int         port = 9000;
    if (argc >= 3) { host = argv[1]; port = atoi(argv[2]); }
    else if (argc == 2) { port = atoi(argv[1]); }

    FlexQL *db = nullptr;
    if (flexql_open(host, port, &db) != FLEXQL_OK) {
        std::cerr << "Cannot connect to FlexQL server at " << host << ":" << port << "\n";
        return 1;
    }
    std::cout << "Connected to FlexQL server\n";

    std::string line, buffer;
    while (true) {
        std::cout << "flexql> ";
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;

        if (line == ".exit" || line == ".quit") { std::cout << "Connection closed\n"; break; }
        if (line == ".help") {
            std::cout << "  .exit  – disconnect\n"
                      << "  SQL;   – execute (must end with ;)\n";
            continue;
        }

        buffer += line + " ";
        if (buffer.find(';') == std::string::npos) continue;

        char *errmsg = nullptr;
        if (flexql_exec(db, buffer.c_str(), printRow, nullptr, &errmsg) != FLEXQL_OK) {
            std::cerr << "Error: " << (errmsg ? errmsg : "unknown") << "\n";
            if (errmsg) flexql_free(errmsg);
        }
        buffer.clear();
    }
    flexql_close(db);
    return 0;
}
