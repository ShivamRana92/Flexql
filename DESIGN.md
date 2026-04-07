# FlexQL Design Document

Github Repository : https://github.com/ShivamRana92/Flexql

## Overview

FlexQL is a lightweight, persistent relational database engine implemented from scratch in C++17. It follows a client-server model over TCP and supports a subset of SQL including CREATE TABLE, INSERT, and SELECT with JOIN and WHERE clauses. The system is designed around three core principles: binary persistence, in-memory indexed access, and LRU query caching.

---

## How Data is Stored

Each table is stored as two files inside the `data/tables/` directory:

- **`<table_name>.schema`** — A plain-text file containing the column definitions including name, type, primary key flag, and not-null constraint.
- **`<table_name>.dat`** — A binary file containing the actual row data.

The data file begins with an 8-byte magic header `FLXDAT10` to identify valid FlexQL data files and guard against reading corrupt or foreign files.

Each row is stored as a length-prefixed binary record. The length is a 4-byte unsigned integer followed by the serialized row bytes. Within a row, each field is encoded by type:

- `DECIMAL` (numeric) columns are stored as 8-byte IEEE 754 doubles.
- `VARCHAR` (string) columns are stored as a 2-byte length prefix followed by the UTF-8 string bytes.
- A single leading byte encodes whether the row is valid (live) or logically deleted.

On server startup, `StorageEngine::loadAll()` scans the data directory for `.schema` files, loads each schema, then opens the corresponding `.dat` file and deserializes all rows back into memory. This restores the full in-memory state from disk without any write-ahead log or recovery procedure.

Inserts are appended to the `.dat` file in real time using `std::ios::app`. A 1 MB write buffer is applied to the file stream to reduce system call overhead on large insert workloads.

---

## Indexing Method

FlexQL maintains a **primary key hash index** in memory for each table. The index is an `unordered_map<string, vector<size_t>>` where the key is the string representation of the primary key value and the value is a list of row indices (positions in the in-memory row array).

The index is built in two situations:

1. **On server startup**, by `StorageEngine::rebuildIndex()`, which iterates all loaded rows and populates the map.
2. **On insert**, where each newly inserted row's primary key is added to the map incrementally.

The primary key index is used by `pkLookup()` for direct key-based access. Full table scans are used for WHERE and JOIN operations, as the current implementation does not maintain secondary indexes.

---

## Caching Strategy

FlexQL uses an **LRU (Least Recently Used) cache** with a default capacity of 512 entries. The cache stores the result sets of SELECT queries keyed by the raw SQL string.

**Cache hits** return the stored result set directly, bypassing parsing, execution, and storage access entirely.

**Cache invalidation** is triggered on every INSERT. When a row is inserted into a table, `cache_.invalidate(table_name)` is called, which scans all cache keys and removes any that contain the table name (case-insensitive match). This ensures stale results are never served after a write.

The LRU eviction policy is implemented using a doubly-linked list (`std::list`) paired with an `unordered_map` for O(1) lookup. On a cache hit, the entry is spliced to the front of the list. On a miss that triggers insertion, the least recently used entry (at the back of the list) is evicted if the cache is at capacity.

---

## Handling of Expiration Timestamps

FlexQL does not implement TTL-based expiration on cached query results in the current version. Cache entries remain valid until either the cache reaches capacity (triggering LRU eviction) or the underlying table is written to (triggering key-based invalidation).

This approach is sufficient for a single-server, write-through workload. Time-based expiration was not required given the system's scope and the use of write-triggered invalidation as the consistency mechanism.

---

## Multithreading Design

The current server is **single-threaded**. The main loop calls `accept()` and then handles each client connection to completion within `handleClient()` before accepting the next connection.

This design was chosen deliberately for simplicity and correctness: it eliminates the need for any mutex protection on the in-memory tables, the pk_index maps, or the LRU cache. All shared state (StorageEngine and LRUCache) is accessed from a single thread of execution, so no data races can occur.

The tradeoff is that one slow client blocks all others. For the use case of benchmarking and exam evaluation with a single connected client at a time, this is not a limitation in practice.

---

## Other Design Decisions

### Idempotent CREATE TABLE

The executor's `execCreate` method silently succeeds if a table already exists, rather than returning an error. This makes it safe to issue CREATE TABLE statements on every server start (as a benchmark or client might do) without needing to check for existence first. The existing table and its data are left untouched.

### Binary Row Format

A custom binary format was chosen over CSV or JSON for row storage because it is compact, fixed-overhead for numeric types, and allows direct deserialization without a text parser. The magic header and per-row length prefix allow detection of truncated or corrupt files and safe iteration.

### Wire Protocol

The server communicates using a simple line-based text protocol over TCP. SQL statements are delimited by semicolons. The server response uses the `ROW` prefix for result rows and `END` as the terminator. This makes the protocol easy to implement in a client without a library, and easy to inspect manually with netcat.

### Value Type System

The `Value` type supports three kinds: `KIND_DOUBLE` (for all numeric types), `KIND_STRING` (for VARCHAR), and `KIND_NULL`. Comparison operators are defined directly on `Value`, allowing the executor to evaluate WHERE conditions generically without type-specific branches in the condition evaluator.

---

## Compilation and Execution Instructions

### Build

```bash
make
```

Produces: `flexql-server`, `flexql-client`, `benchmark_exec`

### Run the server

```bash
./flexql-server
```

The server starts on port 9000 and loads any previously persisted tables from `data/tables/`.

### Run the interactive client

```bash
./flexql-client
```

### Run the benchmark

Because data is persistent, always clear storage before benchmarking to avoid duplicate row accumulation from prior runs:

```bash
rm -rf data/tables/*
./flexql-server &
./benchmark_exec
```

### Clean build artifacts

```bash
make clean        # removes compiled binaries
make clean-data   # removes .dat and .schema files from data/tables/
```

---

## Performance Results

The following results were measured on a standard Linux machine with the server and client running locally.

### Insert Performance

| Rows Inserted | Time       |
|--------------|------------|
| 1,000        | < 5 ms     |
| 10,000       | ~30–60 ms  |
| 100,000      | ~300–600 ms|

Inserts are append-only with a 1 MB write buffer, making them efficient for bulk loading.

### Select Performance (full scan)

| Table Size | Query Time (no cache) | Query Time (cached) |
|------------|----------------------|---------------------|
| 1,000 rows | < 2 ms               | < 1 ms              |
| 10,000 rows| ~5–15 ms             | < 1 ms              |
| 100,000 rows| ~50–150 ms          | < 1 ms              |

The LRU cache eliminates repeated scan cost for repeated identical queries, reducing latency to sub-millisecond regardless of table size.

### JOIN Performance

JOIN is implemented as a nested-loop join. For two tables of size M and N, the complexity is O(M x N). For the test cases in the benchmark (small dimension tables joined against a fact table), this is acceptable. For very large table-to-table joins, a hash join or index-nested-loop would be required.

---

## Summary of Design Choices

| Feature | Choice | Reason |
|---|---|---|
| Storage format | Custom binary with magic header | Compact, fast, no parser overhead |
| Index | In-memory primary key hash map | O(1) point lookup, rebuilt on load |
| Cache | LRU with write invalidation | Eliminates redundant scans, stays consistent |
| Concurrency | Single-threaded | No lock overhead, no races, simple correctness |
| CREATE TABLE | Idempotent | Safe to re-run on each server start |
| Expiration | Not implemented | Write invalidation is sufficient for this scope |
