cmake_minimum_required(VERSION 3.20)
project(rexc LANGUAGES CXX)

# Build the compiler core once, then link it into the CLI and tests.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_library(rexc_core
	src/source.cpp
	src/diagnostics.cpp
	src/ast.cpp
	src/parse.cpp
	src/sema.cpp
	src/ir.cpp
	src/lower_ir.cpp
	src/codegen_x86.cpp
	src/types.cpp
)

target_include_directories(rexc_core PUBLIC include)

add_executable(rexc_tests
	tests/test_main.cpp
	tests/smoke_tests.cpp
	tests/diagnostics_tests.cpp
	tests/frontend_tests.cpp
	tests/sema_tests.cpp
	tests/ir_tests.cpp
	tests/codegen_tests.cpp
	tests/types_tests.cpp
)

target_include_directories(rexc_tests PRIVATE tests)
target_link_libraries(rexc_tests PRIVATE rexc_core)

add_test(NAME rexc_tests COMMAND rexc_tests)

add_executable(rexc
	src/main.cpp
)

target_link_libraries(rexc PRIVATE rexc_core)

add_test(
	NAME cli_smoke
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(
	NAME assemble_smoke
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/assemble_smoke.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)
