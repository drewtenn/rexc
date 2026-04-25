# Root CMake build: fetch ANTLR, generate parser sources, and build Rexc targets.
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
	"${REXC_ANTLR_GENERATED_DIR}/RexcBaseListener.cpp"
	"${REXC_ANTLR_GENERATED_DIR}/RexcLexer.cpp"
	"${REXC_ANTLR_GENERATED_DIR}/RexcListener.cpp"
	"${REXC_ANTLR_GENERATED_DIR}/RexcParser.cpp"
)
set(REXC_ANTLR_GENERATED_HEADERS
	"${REXC_ANTLR_GENERATED_DIR}/RexcBaseListener.h"
	"${REXC_ANTLR_GENERATED_DIR}/RexcLexer.h"
	"${REXC_ANTLR_GENERATED_DIR}/RexcListener.h"
	"${REXC_ANTLR_GENERATED_DIR}/RexcParser.h"
)

add_custom_command(
	OUTPUT ${REXC_ANTLR_GENERATED_SOURCES} ${REXC_ANTLR_GENERATED_HEADERS}
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${REXC_ANTLR_GENERATED_DIR}"
	COMMAND "${Java_JAVA_EXECUTABLE}" -jar "${REXC_ANTLR_JAR}"
	        -Dlanguage=Cpp
	        -Xexact-output-dir
	        -o "${REXC_ANTLR_GENERATED_DIR}"
	        "${CMAKE_CURRENT_SOURCE_DIR}/grammar/Rexc.g4"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/grammar/Rexc.g4" "${REXC_ANTLR_JAR}"
	COMMENT "Generating Rexc ANTLR parser"
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
	src/codegen_x86.cpp
	src/types.cpp
	${REXC_ANTLR_GENERATED_SOURCES}
)

target_include_directories(rexc_core
	PUBLIC include
	PRIVATE "${REXC_ANTLR_GENERATED_DIR}"
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
	tests/types_tests.cpp
)

target_include_directories(rexc_tests PRIVATE tests)
target_link_libraries(rexc_tests PRIVATE rexc_core)

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
