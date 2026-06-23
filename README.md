# Hand-Written Relational Database Engine from Scratch in C++

A complete, production-grade relational database engine implemented entirely from scratch in modern C++ without external library dependencies. The engine features a slotted-page disk manager, a thread-safe buffer pool manager, a latch-crabbed B+ Tree index, a recursive descent SQL parser/lexer, a rule-based query optimizer, and a Volcano-style (Iterator) physical execution engine.

---

## Architecture Overview

The system is organized into modular layers matching the internals of professional relational database management systems (RDBMS):

```
                     +---------------------------------------+
                     |             SQL Query                 |
                     +---------------------------------------+
                                         |
                                         v
                     +---------------------------------------+
                     |           Lexer & Parser              |  [front-end / AST]
                     +---------------------------------------+
                                         |
                                         v
                     +---------------------------------------+
                     |         Planner & Optimizer           |  [RBO / Plan Nodes]
                     +---------------------------------------+
                                         |
                                         v
                     +---------------------------------------+
                     |        Volcano Execution Engine       |  [Physical Executors]
                     +---------------------------------------+
                       |                 |                 |
                       v                 v                 v
            +------------------+ +----------------+ +------------------+
            |  System Catalog  | |   B+ Tree      | |   Table Storage  |
            |  (Metadata)      | |   (Index)      | |   (Slotted Pages)|
            +------------------+ +----------------+ +------------------+
                       \                 |                 /
                        \                |                /
                         v               v               v
                     +---------------------------------------+
                     |         Buffer Pool Manager           |  [Frame Cache]
                     +---------------------------------------+
                                         |
                                         v
                     +---------------------------------------+
                     |            Disk Manager               |  [File Heap / Free List]
                     +---------------------------------------+
                                         |
                                         v
                                    [Disk File]
```

---

## Component Breakdown

### 1. Storage Engine & Cache Layer
*   **Disk Storage Manager**: Manages reading/writing raw `PAGE_SIZE` (4096-byte) blocks. Page 0 stores database header metadata, recycled page ID free-lists, and the overall page count.
*   **LRU Replacer**: Thread-safe cache eviction policy tracking frame status in $O(1)$ time using a hash map combined with a doubly-linked list.
*   **Buffer Pool Manager**: Coordinates in-memory page frame allocations, fetches, and writes dirty pages back to disk.
*   **Thread Safety wrappers**: Custom `Mutex`, `SharedMutex`, and `LockGuard` wrapping native Win32 `CRITICAL_SECTION` to ensure deadlock-free synchronization that compiles out-of-the-box on older MinGW runtimes without standard `<shared_mutex>` support.

### 2. Indexing Layer
*   **B+ Tree Index**: Implements structural B+ Tree operations targeting 64-bit integer keys (`int64_t`) and Record IDs (`RID` containing page ID and slot offset).
*   **Node Splitting**: Automates leaf and internal node splits, parent pointers updates, and page merges recursively.
*   **Latch Crabbing**: Implements concurrency control on B+ Tree pages via read/write latches to permit multi-threaded operations.
*   **Iterator Pattern**: Features the `BPlusTreeIterator` forward-traversal iterator to facilitate range scans.

### 3. Parsing & AST Layer
*   **Lexical Analyzer (Lexer)**: Scans queries to produce tokens, matching SQL keywords case-insensitively and maintaining accurate line/column track metadata for error reporting.
*   **AST Node Tree**: Represents queries polymorphic-ally as nodes (`CreateTableStatement`, `InsertStatement`, `SelectStatement`, `UpdateStatement`, `DeleteStatement`) and expression trees.
*   **Hand-written Recursive Descent Parser**: Converts token arrays into SQL AST statements. Supports arithmetic precedence, relational operators, parenthesis, and compound conditions (`AND`/`OR`).

### 4. Slotted-Page Table Storage & Catalog
*   **Slotted Page Layout**: Serializes dynamic record arrays inside standard disk pages, managing slot offsets and free space boundaries.
*   **Table Heap**: Organizes multi-page linked lists of slotted record pages to support sequential inserts, fetching, and scans.
*   **Catalog**: Tracks schemas (`TableMetadata`) and associated index mappings (`IndexMetadata`).

### 5. Execution Planner & Query Optimizer
*   **Physical Expressions**: Evaluation nodes that resolve dynamic tuple cells based on schemas (`ColumnValueExpression`, `ConstantValueExpression`, `ComparisonExpression`, `LogicalExpression`).
*   **Rule-Based Optimizer (RBO)**: Inspects physical plans and performs optimization passes:
    1.  *Filter Pushdown*: Automatically pushes filter predicates down into sequential scan leaf nodes.
    2.  *Optimize Index Scan*: Replaces costly sequential table scans with B+ Tree index scans (point lookup or range scan) when filter predicates target indexed columns.

### 6. Volcano Executor Engine
*   Implements the Volcano Iterator model interface (`Init()` and `Next()`).
*   Physical execution operators:
    *   `SeqScanExecutor`: Sequentially scans tables, applying pushed-down filters.
    *   `IndexScanExecutor`: Uses the B+ Tree index to execute point lookups or range scans.
    *   `InsertExecutor`: Inserts records and updates associated indexes.
    *   `NestedLoopJoinExecutor`: Joins two child relation streams.
    *   `FilterExecutor`: Resolves complex query predicates.

---

## Directory Structure

```
├── include/              # Header declarations
│   ├── ast.hpp           # AST statement and expression node classes
│   ├── b_plus_tree.hpp   # B+ Tree index template
│   ├── catalog.hpp       # Catalog registry, Schema, Tuple, TableHeap
│   ├── common.hpp        # Core database types (RID, Page IDs) and Win32 locks
│   ├── disk_manager.hpp  # File I/O manager
│   ├── executor.hpp      # Volcano execution iterators
│   ├── lexer.hpp         # Token definitions and Lexer
│   ├── optimizer.hpp     # Plan compilers, RBO optimizer passes
│   ├── page.hpp          # Raw 4096-byte Page class
│   ├── parser.hpp        # Recursive descent parser
│   ├── plan.hpp          # Physical Plan nodes and expressions
│   └── replacer.hpp      # LRU replica eviction pool
├── src/                  # Implementation sources
│   ├── ast.cpp
│   ├── b_plus_tree_internal_page.cpp
│   ├── b_plus_tree_leaf_page.cpp
│   ├── buffer_pool_manager.cpp
│   ├── catalog.cpp
│   ├── disk_manager.cpp
│   ├── executor.cpp
│   ├── lexer.cpp
│   ├── optimizer.cpp
│   ├── page.cpp
│   ├── parser.cpp
│   ├── plan.cpp
│   └── replacer.cpp
├── tests/                # Test suites
│   ├── test_b_plus_tree.cpp
│   ├── test_executor.cpp
│   ├── test_parser.cpp
│   └── test_storage_engine.cpp
├── CMakeLists.txt        # Build configurations
└── README.md             # Project documentation
```

---

## Compiling and Running Tests

The project requires a compiler supporting C++17. All test suites run locally without third-party frameworks.

### Build Commands (using GCC)

#### 1. Compile & Run Storage Engine Tests
```powershell
g++ -std=c++17 -Iinclude src/page.cpp src/disk_manager.cpp src/replacer.cpp src/buffer_pool_manager.cpp tests/test_storage_engine.cpp -o test_storage_engine
.\test_storage_engine.exe
```

#### 2. Compile & Run B+ Tree Index Tests
```powershell
g++ -std=c++17 -Iinclude src/page.cpp src/disk_manager.cpp src/replacer.cpp src/buffer_pool_manager.cpp src/b_plus_tree_internal_page.cpp src/b_plus_tree_leaf_page.cpp tests/test_b_plus_tree.cpp -o test_b_plus_tree
.\test_b_plus_tree.exe
```

#### 3. Compile & Run SQL Parser & Front-end Tests
```powershell
g++ -std=c++17 -Iinclude src/lexer.cpp src/ast.cpp src/parser.cpp tests/test_parser.cpp -o test_parser
.\test_parser.exe
```

#### 4. Compile & Run Catalog, Optimizer, & Execution Engine Tests
```powershell
g++ -std=c++17 -Iinclude src/page.cpp src/disk_manager.cpp src/replacer.cpp src/buffer_pool_manager.cpp src/b_plus_tree_internal_page.cpp src/b_plus_tree_leaf_page.cpp src/lexer.cpp src/ast.cpp src/parser.cpp src/catalog.cpp src/plan.cpp src/executor.cpp src/optimizer.cpp tests/test_executor.cpp -o test_executor
.\test_executor.exe
```
