cmake_minimum_required(VERSION 3.12)
project(APEG_Parser_MVP CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(include)

# MVP: basic + advanced PEG combinators (recognizer only)
add_executable(apeg_parser_mvp
    src/main.cpp
)

# Full paper model: first-class adaptable grammar, attributes/AST,
# Bind / Update / Constraint, extensible-language demos
add_executable(apeg_demo
    src/apeg_demo.cpp
)

# Output directories (convenience for running tests)
set_target_properties(apeg_parser_mvp apeg_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
