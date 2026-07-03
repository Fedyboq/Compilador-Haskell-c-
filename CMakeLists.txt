cmake_minimum_required(VERSION 3.12)
project(APEG_Parser_MVP CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_executable(apeg_parser_mvp
    src/main.cpp
    include/Grammar.cpp
    include/Expr.cpp
)

set_target_properties(apeg_parser_mvp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)