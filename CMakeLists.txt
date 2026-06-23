cmake_minimum_required(VERSION 3.15)
project(SearchEngine)

# ------------------ C++ 标准 ------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------ 输出目录：所有二进制生成到项目根目录 ------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../)

# ==================== Target 1: main ====================
add_executable(main
    DirectoryScanner.cc
    KeywordProcessor.cc
    PageProcessor.cc
    main.cc
)

target_compile_options(main PRIVATE
    -g
)

target_link_libraries(main PRIVATE
    tinyxml2
)

