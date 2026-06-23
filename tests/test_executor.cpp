#include "lexer.hpp"
#include "ast.hpp"
#include "parser.hpp"
#include "catalog.hpp"
#include "plan.hpp"
#include "executor.hpp"
#include "optimizer.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <memory>
#include <cstdio>

void CleanDbFile(const std::string &db_file) {
    std::remove(db_file.c_str());
}

void TestCatalogAndTableStorage() {
    std::cout << "Running TestCatalogAndTableStorage..." << std::endl;
    const std::string db_file = "test_storage.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(10, &dm);
        Catalog catalog(&bpm);

        Schema schema({
            Column("id", TypeID::INT),
            Column("name", TypeID::VARCHAR, 50),
            Column("age", TypeID::INT)
        });

        // Create table
        assert(catalog.CreateTable("users", schema));
        
        // Retrieve table metadata
        auto metadata = catalog.GetTable("users");
        assert(metadata != nullptr);
        assert(metadata->GetName() == "users");
        assert(metadata->GetSchema().GetColumnCount() == 3);

        TableHeap *heap = metadata->GetTableHeap();
        assert(heap != nullptr);

        // Insert tuples
        std::vector<Value> v1 = {Value(1LL), Value("Alice"), Value(25LL)};
        std::vector<Value> v2 = {Value(2LL), Value("Bob"), Value(30LL)};
        std::vector<Value> v3 = {Value(3LL), Value("Charlie"), Value(35LL)};

        Tuple t1(v1, metadata->GetSchema());
        Tuple t2(v2, metadata->GetSchema());
        Tuple t3(v3, metadata->GetSchema());

        RID rid1, rid2, rid3;
        assert(heap->InsertTuple(t1, &rid1));
        assert(heap->InsertTuple(t2, &rid2));
        assert(heap->InsertTuple(t3, &rid3));

        // Read and verify
        Tuple read_t1, read_t2, read_t3;
        assert(heap->GetTuple(rid1, &read_t1));
        assert(heap->GetTuple(rid2, &read_t2));
        assert(heap->GetTuple(rid3, &read_t3));

        assert(read_t1.GetValue(metadata->GetSchema(), 0).GetInt() == 1);
        assert(read_t1.GetValue(metadata->GetSchema(), 1).GetStr() == "Alice");
        assert(read_t1.GetValue(metadata->GetSchema(), 2).GetInt() == 25);

        assert(read_t2.GetValue(metadata->GetSchema(), 0).GetInt() == 2);
        assert(read_t2.GetValue(metadata->GetSchema(), 1).GetStr() == "Bob");
        assert(read_t2.GetValue(metadata->GetSchema(), 2).GetInt() == 30);

        assert(read_t3.GetValue(metadata->GetSchema(), 0).GetInt() == 3);
        assert(read_t3.GetValue(metadata->GetSchema(), 1).GetStr() == "Charlie");
        assert(read_t3.GetValue(metadata->GetSchema(), 2).GetInt() == 35);
    }

    CleanDbFile(db_file);
    std::cout << "TestCatalogAndTableStorage PASSED." << std::endl;
}

void TestQueryPlannerAndOptimizer() {
    std::cout << "Running TestQueryPlannerAndOptimizer..." << std::endl;
    const std::string db_file = "test_opt.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(10, &dm);
        Catalog catalog(&bpm);

        Schema schema({
            Column("id", TypeID::INT),
            Column("name", TypeID::VARCHAR, 50),
            Column("age", TypeID::INT)
        });
        catalog.CreateTable("users", schema);
        
        // Create an index on column id (column index 0)
        assert(catalog.CreateIndex("idx_id", "users", {0}));

        // 1. SELECT query to test Filter Pushdown
        std::string sql = "SELECT id, name FROM users WHERE age = 30";
        Lexer lexer(sql);
        Parser parser(lexer.Tokenize());
        std::unique_ptr<SQLStatement> stmt = parser.Parse();

        Planner planner(&catalog);
        auto initial_plan = planner.Plan(stmt.get());
        assert(initial_plan != nullptr);
        assert(initial_plan->GetType() == PlanType::Filter); // Filter node is at root

        Optimizer optimizer(&catalog);
        auto optimized_plan = optimizer.Optimize(std::move(initial_plan));
        assert(optimized_plan != nullptr);
        // Filter has been pushed down into SeqScan
        assert(optimized_plan->GetType() == PlanType::SeqScan);
        auto scan = static_cast<const SeqScanPlanNode*>(optimized_plan.get());
        assert(scan->GetFilterPredicate() != nullptr);

        // 2. SELECT query to test Index Scan Replacement
        std::string sql_idx = "SELECT id, name FROM users WHERE id = 42";
        Lexer lexer2(sql_idx);
        Parser parser2(lexer2.Tokenize());
        std::unique_ptr<SQLStatement> stmt2 = parser2.Parse();

        auto plan2 = planner.Plan(stmt2.get());
        auto optimized_plan2 = optimizer.Optimize(std::move(plan2));
        assert(optimized_plan2 != nullptr);
        // SeqScan replaced with IndexScan
        assert(optimized_plan2->GetType() == PlanType::IndexScan);
        auto idx_scan = static_cast<const IndexScanPlanNode*>(optimized_plan2.get());
        assert(idx_scan->GetIndexName() == "idx_id");
        assert(idx_scan->GetScanPredicate() != nullptr);
    }

    CleanDbFile(db_file);
    std::cout << "TestQueryPlannerAndOptimizer PASSED." << std::endl;
}

void TestExecutors() {
    std::cout << "Running TestExecutors..." << std::endl;
    const std::string db_file = "test_exec.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(50, &dm);
        Catalog catalog(&bpm);

        Schema schema({
            Column("id", TypeID::INT),
            Column("name", TypeID::VARCHAR, 50),
            Column("age", TypeID::INT)
        });
        catalog.CreateTable("users", schema);
        catalog.CreateIndex("idx_id", "users", {0});

        auto metadata = catalog.GetTable("users");
        TableHeap *heap = metadata->GetTableHeap();

        // Populate table
        std::vector<std::pair<int64_t, std::string>> source_data = {
            {10, "Alice"}, {20, "Bob"}, {30, "Charlie"}, {40, "David"}, {50, "Eve"}
        };
        for (const auto &p : source_data) {
            Tuple tuple({Value(p.first), Value(p.second), Value(25LL + p.first / 10)}, metadata->GetSchema());
            RID rid;
            assert(heap->InsertTuple(tuple, &rid));
            // Index insert
            catalog.GetIndex("idx_id")->GetIndex()->Insert(p.first, rid);
        }

        // 1. SeqScan with Filter: SELECT * FROM users WHERE age > 27
        // (age > 27 matches Bob(age 27, wait, no: Bob is age 25 + 2 = 27), Charlie(age 28), David(age 29), Eve(age 30))
        std::string sql_seq = "SELECT id, name FROM users WHERE age > 27";
        Lexer lexer_seq(sql_seq);
        Parser parser_seq(lexer_seq.Tokenize());
        auto stmt_seq = parser_seq.Parse();

        Planner planner(&catalog);
        auto plan_seq = planner.Plan(stmt_seq.get());
        Optimizer optimizer(&catalog);
        auto optimized_plan_seq = optimizer.Optimize(std::move(plan_seq));
        assert(optimized_plan_seq->GetType() == PlanType::SeqScan);

        auto exec_seq = BuildExecutor(optimized_plan_seq.get(), &catalog, &bpm);
        exec_seq->Init();

        Tuple t;
        RID r;
        std::vector<std::string> results;
        while (exec_seq->Next(&t, &r)) {
            results.push_back(t.GetValue(metadata->GetSchema(), 1).GetStr());
        }
        assert(results.size() == 3);
        assert(results[0] == "Charlie");
        assert(results[1] == "David");
        assert(results[2] == "Eve");

        // 2. Index Scan Point Lookup: SELECT * FROM users WHERE id = 30
        std::string sql_idx = "SELECT id, name FROM users WHERE id = 30";
        Lexer lexer_idx(sql_idx);
        Parser parser_idx(lexer_idx.Tokenize());
        auto stmt_idx = parser_idx.Parse();

        auto plan_idx = planner.Plan(stmt_idx.get());
        auto optimized_plan_idx = optimizer.Optimize(std::move(plan_idx));
        assert(optimized_plan_idx->GetType() == PlanType::IndexScan);

        auto exec_idx = BuildExecutor(optimized_plan_idx.get(), &catalog, &bpm);
        exec_idx->Init();

        Tuple t_idx;
        RID r_idx;
        assert(exec_idx->Next(&t_idx, &r_idx));
        assert(t_idx.GetValue(metadata->GetSchema(), 0).GetInt() == 30);
        assert(t_idx.GetValue(metadata->GetSchema(), 1).GetStr() == "Charlie");
        assert(!exec_idx->Next(&t_idx, &r_idx));

        // 3. Index Scan Range Scan: SELECT * FROM users WHERE id >= 30
        std::string sql_range = "SELECT id, name FROM users WHERE id >= 30";
        Lexer lexer_range(sql_range);
        Parser parser_range(lexer_range.Tokenize());
        auto stmt_range = parser_range.Parse();

        auto plan_range = planner.Plan(stmt_range.get());
        auto optimized_plan_range = optimizer.Optimize(std::move(plan_range));
        assert(optimized_plan_range->GetType() == PlanType::IndexScan);

        auto exec_range = BuildExecutor(optimized_plan_range.get(), &catalog, &bpm);
        exec_range->Init();

        std::vector<std::string> range_results;
        Tuple t_range;
        RID r_range;
        while (exec_range->Next(&t_range, &r_range)) {
            range_results.push_back(t_range.GetValue(metadata->GetSchema(), 1).GetStr());
        }
        assert(range_results.size() == 3);
        assert(range_results[0] == "Charlie");
        assert(range_results[1] == "David");
        assert(range_results[2] == "Eve");
    }

    CleanDbFile(db_file);
    std::cout << "TestExecutors PASSED." << std::endl;
}

void TestJoin() {
    std::cout << "Running TestJoin..." << std::endl;
    const std::string db_file = "test_join.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(50, &dm);
        Catalog catalog(&bpm);

        // Create table: users
        Schema user_schema({
            Column("uid", TypeID::INT),
            Column("uname", TypeID::VARCHAR, 50)
        });
        catalog.CreateTable("users", user_schema);
        auto users_metadata = catalog.GetTable("users");
        
        // Create table: orders
        Schema order_schema({
            Column("oid", TypeID::INT),
            Column("uid", TypeID::INT),
            Column("item", TypeID::VARCHAR, 50)
        });
        catalog.CreateTable("orders", order_schema);
        auto orders_metadata = catalog.GetTable("orders");

        // Insert user tuples
        RID r;
        users_metadata->GetTableHeap()->InsertTuple(Tuple({Value(1LL), Value("Alice")}, user_schema), &r);
        users_metadata->GetTableHeap()->InsertTuple(Tuple({Value(2LL), Value("Bob")}, user_schema), &r);

        // Insert order tuples
        orders_metadata->GetTableHeap()->InsertTuple(Tuple({Value(100LL), Value(1LL), Value("Laptop")}, order_schema), &r);
        orders_metadata->GetTableHeap()->InsertTuple(Tuple({Value(101LL), Value(2LL), Value("Phone")}, order_schema), &r);
        orders_metadata->GetTableHeap()->InsertTuple(Tuple({Value(102LL), Value(1LL), Value("Book")}, order_schema), &r);

        // We will manually build a NestedLoopJoinPlanNode since our planner currently plans single-table SELECTs.
        // Joint schema: uid (INT), uname (VARCHAR), oid (INT), uid2 (INT), item (VARCHAR)
        Schema joined_schema({
            Column("uid", TypeID::INT),
            Column("uname", TypeID::VARCHAR, 50),
            Column("oid", TypeID::INT),
            Column("uid2", TypeID::INT),
            Column("item", TypeID::VARCHAR, 50)
        });

        auto scan_users = std::make_unique<SeqScanPlanNode>(user_schema, "users");
        auto scan_orders = std::make_unique<SeqScanPlanNode>(order_schema, "orders");

        // Join predicate: uid = uid2
        // Column Value expression for outer uid: idx 0 in user_schema
        // Column Value expression for inner uid: idx 1 in order_schema (offset to idx 3 in joined_schema)
        auto join_predicate = std::make_unique<ComparisonExpression>(
            std::make_unique<ColumnValueExpression>(0, TypeID::INT),
            "=",
            std::make_unique<ColumnValueExpression>(3, TypeID::INT)
        );

        auto join_plan = std::make_unique<NestedLoopJoinPlanNode>(
            joined_schema,
            std::move(scan_users),
            std::move(scan_orders),
            std::move(join_predicate)
        );

        auto executor = BuildExecutor(join_plan.get(), &catalog, &bpm);
        executor->Init();

        Tuple t;
        RID rid;
        std::vector<std::string> joined_rows;
        while (executor->Next(&t, &rid)) {
            std::string uname = t.GetValue(joined_schema, 1).GetStr();
            std::string item = t.GetValue(joined_schema, 4).GetStr();
            joined_rows.push_back(uname + " bought " + item);
        }

        assert(joined_rows.size() == 3);
        assert(joined_rows[0] == "Alice bought Laptop");
        assert(joined_rows[1] == "Alice bought Book");
        assert(joined_rows[2] == "Bob bought Phone");
    }

    CleanDbFile(db_file);
    std::cout << "TestJoin PASSED." << std::endl;
}

int main() {
    try {
        TestCatalogAndTableStorage();
        TestQueryPlannerAndOptimizer();
        TestExecutors();
        TestJoin();
        std::cout << "\nALL CATALOG, OPTIMIZER & EXECUTOR TESTS PASSED SUCCESSFULLY!" << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
