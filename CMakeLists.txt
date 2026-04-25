cmake_minimum_required(VERSION 3.20)
project(rexc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_library(rexc_core
	src/source.cpp
	src/diagnostics.cpp
	src/ast.cpp
	src/parse.cpp
)

target_include_directories(rexc_core PUBLIC include)

add_executable(rexc_tests
	tests/test_main.cpp
	tests/smoke_tests.cpp
	tests/diagnostics_tests.cpp
	tests/frontend_tests.cpp
)

target_include_directories(rexc_tests PRIVATE tests)
target_link_libraries(rexc_tests PRIVATE rexc_core)

add_test(NAME rexc_tests COMMAND rexc_tests)
