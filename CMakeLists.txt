# Root CMake build: fetch ANTLR, generate parser sources, and build rexc targets.
cmake_minimum_required(VERSION 3.20)
project(rexc LANGUAGES CXX)

# Build the compiler core once, then link it into the CLI and tests.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

include(FetchContent)
find_package(Java COMPONENTS Runtime REQUIRED)

set(REXC_ANTLR_VERSION 4.13.2)
set(REXC_ANTLR_JAR "${CMAKE_CURRENT_BINARY_DIR}/antlr-${REXC_ANTLR_VERSION}-complete.jar")

if(NOT EXISTS "${REXC_ANTLR_JAR}")
	file(
		DOWNLOAD
		"https://www.antlr.org/download/antlr-${REXC_ANTLR_VERSION}-complete.jar"
		"${REXC_ANTLR_JAR}"
		EXPECTED_HASH SHA256=eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76
		TLS_VERIFY ON
	)
endif()

set(WITH_DEMO OFF CACHE BOOL "" FORCE)
set(ANTLR4_INSTALL OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
	antlr4cpp
	URL "https://www.antlr.org/download/antlr4-cpp-runtime-${REXC_ANTLR_VERSION}-source.zip"
	URL_HASH SHA256=0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(antlr4cpp)

set(REXC_ANTLR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/antlr")
set(REXC_ANTLR_GENERATED_SOURCES
	"${REXC_ANTLR_GENERATED_DIR}/RexyBaseListener.cpp"
	"${REXC_ANTLR_GENERATED_DIR}/RexyLexer.cpp"
	"${REXC_ANTLR_GENERATED_DIR}/RexyListener.cpp"
	"${REXC_ANTLR_GENERATED_DIR}/RexyParser.cpp"
)
set(REXC_ANTLR_GENERATED_HEADERS
	"${REXC_ANTLR_GENERATED_DIR}/RexyBaseListener.h"
	"${REXC_ANTLR_GENERATED_DIR}/RexyLexer.h"
	"${REXC_ANTLR_GENERATED_DIR}/RexyListener.h"
	"${REXC_ANTLR_GENERATED_DIR}/RexyParser.h"
)

add_custom_command(
	OUTPUT ${REXC_ANTLR_GENERATED_SOURCES} ${REXC_ANTLR_GENERATED_HEADERS}
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${REXC_ANTLR_GENERATED_DIR}"
	COMMAND "${Java_JAVA_EXECUTABLE}" -jar "${REXC_ANTLR_JAR}"
	        -Dlanguage=Cpp
	        -Xexact-output-dir
	        -o "${REXC_ANTLR_GENERATED_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}/grammar/Rexy.g4"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/grammar/Rexy.g4" "${REXC_ANTLR_JAR}"
	COMMENT "Generating Rexy ANTLR parser"
	VERBATIM
)

set(REXC_STDLIB_RX_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/core/str.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/core/mem.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/core/num.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/core/option.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/core/result.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/core/hash.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/alloc/alloc.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/std/io.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/std/process.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/std/fs.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/std/path.rx"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/std/time.rx"
	# FE-109a: src/stdlib/std/vec.rx is intentionally NOT included here
	# yet. The stdlib parser (src/stdlib/stdlib.cpp::resolve_source_unit)
	# skips generic structs and generic functions, so registering vec.rx
	# would have no user-visible effect. Once that resolver grows
	# generic-template support, add this entry. In the meantime the
	# generic Vec<T> contract is exercised end-to-end by
	# `examples/generic_vec_demo.rx`, which declares the same shape
	# inline. The vec.rx file is kept in-tree as the canonical API
	# reference.
)

set(REXC_STDLIB_GENERATED_SOURCE
	"${CMAKE_CURRENT_BINARY_DIR}/generated/stdlib/source.cpp"
)

add_custom_command(
	OUTPUT "${REXC_STDLIB_GENERATED_SOURCE}"
	COMMAND "${CMAKE_COMMAND}" -E make_directory
	        "${CMAKE_CURRENT_BINARY_DIR}/generated/stdlib"
	COMMAND "${CMAKE_COMMAND}"
	        "-DREXC_STDLIB_RX_SOURCES=${REXC_STDLIB_RX_SOURCES}"
	        "-DREXC_STDLIB_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/source.cpp.in"
	        "-DREXC_STDLIB_OUTPUT=${REXC_STDLIB_GENERATED_SOURCE}"
	        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_stdlib_source.cmake"
	DEPENDS
	        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_stdlib_source.cmake"
	        "${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/source.cpp.in"
	        ${REXC_STDLIB_RX_SOURCES}
	COMMENT "Embedding Rexy stdlib source"
	VERBATIM
)

add_library(rexc_core
	src/source.cpp
	src/diagnostics.cpp
	src/ast.cpp
	src/parse.cpp
	src/sema.cpp
	src/ir.cpp
	src/lower_ir.cpp
	src/target.cpp
	src/codegen_x86.cpp
	src/codegen_arm64.cpp
	src/types.cpp
	src/stdlib/stdlib.cpp
	"${REXC_STDLIB_GENERATED_SOURCE}"
	src/stdlib/sys/runtime_i386_linux.cpp
	src/stdlib/sys/runtime_i386_drunix.cpp
	src/stdlib/sys/runtime_x86_64_linux.cpp
	src/stdlib/sys/runtime_arm64_macos.cpp
	src/stdlib/sys/runtime_arm64_drunix.cpp
	${REXC_ANTLR_GENERATED_SOURCES}
)

target_include_directories(rexc_core
	PUBLIC include
	PRIVATE src "${REXC_ANTLR_GENERATED_DIR}"
)
target_link_libraries(rexc_core PRIVATE antlr4_static)

add_executable(rexc_tests
	tests/test_main.cpp
	tests/smoke_tests.cpp
	tests/diagnostics_tests.cpp
	tests/frontend_tests.cpp
	tests/sema_tests.cpp
	tests/ir_tests.cpp
	tests/codegen_tests.cpp
	tests/codegen_arm64_tests.cpp
	tests/target_tests.cpp
	tests/types_tests.cpp
	tests/stdlib_tests.cpp
)

target_include_directories(rexc_tests PRIVATE tests)
target_link_libraries(rexc_tests PRIVATE rexc_core)
target_compile_definitions(rexc_tests PRIVATE
	REXC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(NAME rexc_tests COMMAND rexc_tests)

add_test(
	NAME parser_architecture
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/parser_architecture.sh"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

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

# ---------------------------------------------------------------------------
# rxy — Cargo-like package manager for Rexy. Sibling tool to rexc.
# Phase A: manifest + lockfile + single-package build that shells out to rexc.
# See docs/prd-package-manager.md.
# ---------------------------------------------------------------------------

# tomlplusplus pinned to the immutable commit for tag v3.4.0 rather than the
# tag itself. Mirrors how the ANTLR fetch above pins by SHA hash; tags can be
# moved upstream, commits cannot.
FetchContent_Declare(
	tomlplusplus
	GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
	GIT_TAG        30172438cee64926dc41fdd9c11fb3ba5b2ba9de  # v3.4.0
	GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

add_library(rxy_core STATIC
	src/rxy/cli/cli.cpp
	src/rxy/manifest/manifest.cpp
	src/rxy/lockfile/lockfile.cpp
	src/rxy/build/build.cpp
	src/rxy/process/process.cpp
	src/rxy/diag/diag.cpp
	src/rxy/util/fs.cpp
	src/rxy/util/tty.cpp
	src/rxy/todo_impl.cpp
	src/rxy/hash/sha256.cpp
	src/rxy/cache/cache.cpp
	src/rxy/source/source.cpp
	src/rxy/resolver/resolver.cpp
	src/rxy/semver/semver.cpp
	src/rxy/registry/registry.cpp
	src/rxy/build_script/build_script.cpp
	src/rxy/workspace/workspace.cpp
)
target_include_directories(rxy_core PUBLIC src/rxy)
target_link_libraries(rxy_core PUBLIC tomlplusplus::tomlplusplus)
target_compile_definitions(rxy_core PUBLIC RXY_VERSION="0.1.0")

# posix_spawn_file_actions_addchdir_np: macOS 10.15+ and glibc 2.29+ (April 2019).
# When present, lets posix_spawn perform the cwd change in the child rather than
# requiring the parent to chdir() before spawn (which races with concurrent calls).
include(CheckSymbolExists)
check_symbol_exists(posix_spawn_file_actions_addchdir_np "spawn.h"
	HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
if(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
	target_compile_definitions(rxy_core PRIVATE RXY_HAVE_POSIX_SPAWN_ADDCHDIR_NP=1)
endif()

add_executable(rxy src/rxy/main.cpp)
target_link_libraries(rxy PRIVATE rxy_core)

add_executable(rxy_tests
	tests/rxy/test_main.cpp
	tests/rxy/manifest_tests.cpp
	tests/rxy/lockfile_tests.cpp
	tests/rxy/cli_tests.cpp
	tests/rxy/sha256_tests.cpp
	tests/rxy/deps_tests.cpp
	tests/rxy/semver_tests.cpp
	tests/rxy/registry_tests.cpp
	tests/rxy/build_script_tests.cpp
	tests/rxy/workspace_tests.cpp
	tests/rxy/offline_tests.cpp
	tests/rxy/cache_tests.cpp
	tests/rxy/process_tests.cpp
)
target_include_directories(rxy_tests PRIVATE tests/rxy)
target_link_libraries(rxy_tests PRIVATE rxy_core)
target_compile_definitions(rxy_tests PRIVATE
	RXY_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy"
)
add_test(NAME rxy_tests COMMAND rxy_tests)

add_test(
	NAME rxy_smoke
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy/smoke.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(
	NAME rxy_smoke_b
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy/smoke_b.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(
	NAME rxy_smoke_c
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy/smoke_c.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(
	NAME rxy_smoke_d
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy/smoke_d.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(
	NAME rxy_smoke_e
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy/smoke_e.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(
	NAME rxy_smoke_f
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy/smoke_f.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(
	NAME rxy_smoke_g
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy/smoke_g.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(
	NAME rxy_smoke_h
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/rxy/smoke_h.sh"
	        "${CMAKE_CURRENT_BINARY_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}"
)
