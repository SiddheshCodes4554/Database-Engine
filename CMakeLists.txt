cmake_minimum_required(VERSION 3.15)
project(DatabaseEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

set(SOURCES
    src/page.cpp
    src/disk_manager.cpp
    src/replacer.cpp
    src/buffer_pool_manager.cpp
    src/b_plus_tree_internal_page.cpp
    src/b_plus_tree_leaf_page.cpp
)

add_library(db_storage ${SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(db_storage PRIVATE Threads::Threads)

add_executable(test_storage_engine tests/test_storage_engine.cpp)
target_link_libraries(test_storage_engine PRIVATE db_storage Threads::Threads)

add_executable(test_b_plus_tree tests/test_b_plus_tree.cpp)
target_link_libraries(test_b_plus_tree PRIVATE db_storage Threads::Threads)
