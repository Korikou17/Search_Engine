cmake_minimum_required(VERSION 3.15)
project(SearchEngine)

# ------------------ C++ 标准 ------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------ 输出目录：所有二进制生成到 bin/ ------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

find_package(spdlog REQUIRED)

# ------------------ 公共库：编解码器 ------------------
add_library(search_common STATIC
    common/codec/TLVCodec.cc
)
target_include_directories(search_common PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(search_common PUBLIC muduo_net muduo_base spdlog::spdlog)

# ==================== Target 1: offline_indexer (离线索引构建) ====================
add_executable(offline_indexer
    indexer/main.cc
    indexer/DirectoryScanner.cc
    indexer/KeywordProcessor.cc
    indexer/PageProcessor.cc
)
target_include_directories(offline_indexer PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(offline_indexer PRIVATE -g)
target_link_libraries(offline_indexer PRIVATE tinyxml2 spdlog::spdlog)

# ==================== Target 2: server (在线搜索服务) ====================
add_executable(server
    server/main.cc
    server/SearchServer.cc
    server/search/PageSearcher.cc
    server/recommend/KeywordRecommend.cc
)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(server PRIVATE -g)
target_link_libraries(server PRIVATE search_common muduo_net muduo_base spdlog::spdlog tinyxml2)

# ==================== Target 3: client (搜索客户端) ====================
add_executable(client
    client/main.cc
    client/SearchClient.cc
)
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(client PRIVATE -g)
target_link_libraries(client PRIVATE search_common muduo_net muduo_base spdlog::spdlog)

