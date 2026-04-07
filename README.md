# FlexQL

A lightweight, persistent SQL database engine built from scratch in C++17. FlexQL implements a client-server architecture over TCP, supporting CREATE TABLE, INSERT, and SELECT (with JOIN and WHERE) operations with binary file-based storage and LRU query caching.

---

## Project Structure

```
flexql_final/
├── src/
│   ├── server/
│   │   ├── server.cpp       # TCP server, connection handling
│   │   ├── parser.cpp/.h    # SQL parser
│   │   ├── executor.cpp/.h  # Statement executor
│   │   ├── storage.cpp/.h   # Binary storage engine
│   │   ├── cache.cpp/.h     # LRU cache
│   │   └── types.h          # Core data types
│   └── client/
│       ├── flexql_api.cpp   # Client API implementation
│       └── repl.cpp         # Interactive REPL client
├── benchmark/
│   └── benchmark_flexql.cpp # Performance benchmark suite
├── include/
│   └── flexql.h             # Public client API header
├── data/
│   └── tables/              # Persistent storage directory
└── Makefile
```

---

## Prerequisites

- g++ with C++17 support
- Linux (uses POSIX sockets and filesystem APIs)
- make

---

## Build

```bash
make
```

This produces three binaries: `flexql-server`, `flexql-client`, and `benchmark_exec`.

To clean compiled binaries:

```bash
make clean
```

To also remove stored table data:

```bash
make clean-data
```

---

## Running

**Start the server** (listens on port 9000):

```bash
./flexql-server
```

**Start the interactive client** (in a separate terminal):

```bash
./flexql-client
```

**Run the benchmark suite:**

```bash
# Always clear old data before benchmarking to avoid duplicates from persistent storage
rm -rf data/tables/*
./flexql-server &
./benchmark_exec
```

---

## Supported SQL

```sql
-- Create a table
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, age INT);

-- Insert rows
INSERT INTO users VALUES (1, 'Alice', 30);

-- Select all rows
SELECT * FROM users;

-- Select with WHERE
SELECT * FROM users WHERE age > 25;

-- Select with JOIN
SELECT * FROM users JOIN orders ON users.id = orders.user_id;
```

---

## Persistence Notes

Data is stored persistently in `data/tables/` as `.dat` (binary row data) and `.schema` (column definitions) files. Tables and their data survive server restarts.

Because of this, running the benchmark multiple times without clearing storage will accumulate duplicate rows. Always clear `data/tables/` before running benchmarks:

```bash
rm -rf data/tables/*
```

This is expected behavior — it demonstrates that persistence is working correctly.

---

## Wire Protocol

The server communicates over TCP on port 9000. SQL statements are terminated with a semicolon. Responses follow this format:

- Non-SELECT success: `OK\nEND\n`
- SELECT results: one `ROW ...` line per result row, followed by `END\n`
- Errors: `ERROR: <message>\nEND\n`
