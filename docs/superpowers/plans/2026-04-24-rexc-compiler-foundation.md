# Rexc Compiler Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first Rexc compiler scaffold: C++17 source, ANTLR grammar, AST, semantic checks, typed IR, i386 assembly output, CLI, examples, and tests.

**Architecture:** The compiler reads one `.rx` file, parses it with ANTLR-generated C++ code, converts the parse tree into a compiler-owned AST, validates symbols and types, lowers to a simple typed IR, then emits 32-bit x86 GNU assembler syntax. The parser-generator boundary is isolated in `src/parse/` and `src/frontend/` so semantic analysis and code generation do not depend on ANTLR parse-tree classes.

**Tech Stack:** C++17, CMake, ANTLR 4 C++ runtime, GoogleTest-style executable tests implemented with simple assertions if GoogleTest is unavailable, POSIX shell smoke tests, GNU assembler-compatible i386 assembly.

---

## File Structure

- Create `CMakeLists.txt`: root build, C++17 settings, ANTLR generation hook, compiler executable, unit test executable.
- Create `cmake/FindANTLR.cmake`: locate `antlr4` jar/tool and `antlr4-runtime` headers/library.
- Create `grammar/Rexc.g4`: language grammar for the initial syntax subset.
- Create `include/rexc/source.hpp` and `src/source.cpp`: source file storage and line/column lookup.
- Create `include/rexc/diagnostics.hpp` and `src/diagnostics.cpp`: diagnostics collection and formatting.
- Create `include/rexc/ast.hpp` and `src/ast.cpp`: compiler-owned AST nodes.
- Create `include/rexc/parse.hpp` and `src/parse.cpp`: wrapper around ANTLR parser invocation.
- Create `include/rexc/ast_builder.hpp` and `src/ast_builder.cpp`: parse-tree to AST conversion.
- Create `include/rexc/sema.hpp` and `src/sema.cpp`: symbol binding and type checks.
- Create `include/rexc/ir.hpp` and `src/ir.cpp`: typed IR model.
- Create `include/rexc/lower_ir.hpp` and `src/lower_ir.cpp`: AST-to-IR lowering.
- Create `include/rexc/codegen_x86.hpp` and `src/codegen_x86.cpp`: assembly emitter.
- Create `src/main.cpp`: `rexc input.rx -S -o output.s` CLI.
- Create `tests/test_main.cpp`: shared tiny test runner.
- Create `tests/frontend_tests.cpp`: parser and AST tests.
- Create `tests/sema_tests.cpp`: semantic diagnostics tests.
- Create `tests/codegen_tests.cpp`: IR/codegen golden tests.
- Create `tests/cli_smoke.sh`: CLI compile smoke tests.
- Create `examples/add.rx`: minimal example program.
- Create `README.md`: build and usage notes.

## Task 1: Build Skeleton And Test Harness

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/test_main.cpp`
- Create: `tests/smoke_tests.cpp`
- Create: `README.md`

- [ ] **Step 1: Write the failing smoke test**

Create `tests/test_main.cpp`:

```cpp
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct TestCase {
	const char *name;
	std::function<void()> run;
};

std::vector<TestCase> &test_registry()
{
	static std::vector<TestCase> tests;
	return tests;
}

void register_test(const char *name, std::function<void()> run)
{
	test_registry().push_back({name, run});
}

int main()
{
	int failed = 0;
	for (const auto &test : test_registry()) {
		try {
			test.run();
			std::cout << "PASS " << test.name << "\n";
		} catch (const std::exception &err) {
			++failed;
			std::cerr << "FAIL " << test.name << ": " << err.what() << "\n";
		}
	}
	return failed == 0 ? 0 : 1;
}
```

Create `tests/smoke_tests.cpp`:

```cpp
#include <stdexcept>
#include <string>

void register_test(const char *name, std::function<void()> run);

#define TEST_CASE(name) \
	static void name(); \
	static const bool name##_registered = [] { \
		register_test(#name, name); \
		return true; \
	}(); \
	static void name()

#define REQUIRE(value) \
	do { \
		if (!(value)) \
			throw std::runtime_error("requirement failed: " #value); \
	} while (false)

TEST_CASE(smoke_test_runner_executes)
{
	REQUIRE(std::string("rexc") == "rexc");
}
```

- [ ] **Step 2: Run test to verify it fails before build files exist**

Run:

```sh
cmake -S . -B build
```

Expected: FAIL because `CMakeLists.txt` does not exist.

- [ ] **Step 3: Add minimal CMake build**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(rexc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(rexc_tests
	tests/test_main.cpp
	tests/smoke_tests.cpp
)

target_include_directories(rexc_tests PRIVATE include)

add_test(NAME rexc_tests COMMAND rexc_tests)
```

Create `README.md`:

```markdown
# Rexc

Rexc is an experimental systems-language compiler for Drunix userland.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
```

Fix `tests/smoke_tests.cpp` to include `<functional>`:

```cpp
#include <functional>
#include <stdexcept>
#include <string>

void register_test(const char *name, std::function<void()> run);

#define TEST_CASE(name) \
	static void name(); \
	static const bool name##_registered = [] { \
		register_test(#name, name); \
		return true; \
	}(); \
	static void name()

#define REQUIRE(value) \
	do { \
		if (!(value)) \
			throw std::runtime_error("requirement failed: " #value); \
	} while (false)

TEST_CASE(smoke_test_runner_executes)
{
	REQUIRE(std::string("rexc") == "rexc");
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS with `100% tests passed`.

- [ ] **Step 5: Commit**

```sh
git add CMakeLists.txt README.md tests/test_main.cpp tests/smoke_tests.cpp
git commit -m "build: add CMake test harness"
```

## Task 2: Source And Diagnostics Foundation

**Files:**
- Create: `include/rexc/source.hpp`
- Create: `src/source.cpp`
- Create: `include/rexc/diagnostics.hpp`
- Create: `src/diagnostics.cpp`
- Create: `tests/diagnostics_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing diagnostics tests**

Create `tests/diagnostics_tests.cpp`:

```cpp
#include "rexc/diagnostics.hpp"
#include "rexc/source.hpp"

#include <functional>
#include <stdexcept>
#include <string>

void register_test(const char *name, std::function<void()> run);

#define TEST_CASE(name) \
	static void name(); \
	static const bool name##_registered = [] { \
		register_test(#name, name); \
		return true; \
	}(); \
	static void name()

#define REQUIRE(value) \
	do { \
		if (!(value)) \
			throw std::runtime_error("requirement failed: " #value); \
	} while (false)

TEST_CASE(source_maps_offsets_to_line_and_column)
{
	rexc::SourceFile source("test.rx", "fn main()\n  return 1;\n");
	auto loc = source.location_at(12);
	REQUIRE(loc.line == 2);
	REQUIRE(loc.column == 3);
}

TEST_CASE(diagnostics_format_with_source_location)
{
	rexc::SourceFile source("test.rx", "fn main()\n  return 1;\n");
	rexc::Diagnostics diagnostics;
	diagnostics.error(source.location_at(12), "expected expression");
	REQUIRE(diagnostics.has_errors());
	REQUIRE(diagnostics.format() == "test.rx:2:3: error: expected expression\n");
}
```

Modify `CMakeLists.txt` to add the new test file and library that does not yet exist:

```cmake
add_library(rexc_core
	src/source.cpp
	src/diagnostics.cpp
)

target_include_directories(rexc_core PUBLIC include)

add_executable(rexc_tests
	tests/test_main.cpp
	tests/smoke_tests.cpp
	tests/diagnostics_tests.cpp
)

target_link_libraries(rexc_tests PRIVATE rexc_core)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: FAIL because `rexc/source.hpp` and `rexc/diagnostics.hpp` do not exist.

- [ ] **Step 3: Implement source and diagnostics**

Create `include/rexc/source.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rexc {

struct SourceLocation {
	std::string file;
	std::size_t offset = 0;
	std::size_t line = 1;
	std::size_t column = 1;
};

class SourceFile {
public:
	SourceFile(std::string path, std::string text);

	const std::string &path() const;
	const std::string &text() const;
	SourceLocation location_at(std::size_t offset) const;

private:
	std::string path_;
	std::string text_;
	std::vector<std::size_t> line_starts_;
};

} // namespace rexc
```

Create `src/source.cpp`:

```cpp
#include "rexc/source.hpp"

#include <algorithm>

namespace rexc {

SourceFile::SourceFile(std::string path, std::string text)
	: path_(std::move(path)), text_(std::move(text))
{
	line_starts_.push_back(0);
	for (std::size_t i = 0; i < text_.size(); ++i) {
		if (text_[i] == '\n')
			line_starts_.push_back(i + 1);
	}
}

const std::string &SourceFile::path() const
{
	return path_;
}

const std::string &SourceFile::text() const
{
	return text_;
}

SourceLocation SourceFile::location_at(std::size_t offset) const
{
	if (offset > text_.size())
		offset = text_.size();

	auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
	std::size_t line_index = static_cast<std::size_t>(it - line_starts_.begin() - 1);
	std::size_t line_start = line_starts_[line_index];

	return SourceLocation{path_, offset, line_index + 1, offset - line_start + 1};
}

} // namespace rexc
```

Create `include/rexc/diagnostics.hpp`:

```cpp
#pragma once

#include "rexc/source.hpp"

#include <string>
#include <vector>

namespace rexc {

enum class DiagnosticSeverity {
	Error,
};

struct Diagnostic {
	DiagnosticSeverity severity;
	SourceLocation location;
	std::string message;
};

class Diagnostics {
public:
	void error(SourceLocation location, std::string message);
	bool has_errors() const;
	const std::vector<Diagnostic> &items() const;
	std::string format() const;

private:
	std::vector<Diagnostic> items_;
};

} // namespace rexc
```

Create `src/diagnostics.cpp`:

```cpp
#include "rexc/diagnostics.hpp"

#include <sstream>

namespace rexc {

void Diagnostics::error(SourceLocation location, std::string message)
{
	items_.push_back({DiagnosticSeverity::Error, std::move(location), std::move(message)});
}

bool Diagnostics::has_errors() const
{
	return !items_.empty();
}

const std::vector<Diagnostic> &Diagnostics::items() const
{
	return items_;
}

std::string Diagnostics::format() const
{
	std::ostringstream out;
	for (const auto &item : items_) {
		out << item.location.file << ':' << item.location.line << ':'
		    << item.location.column << ": error: " << item.message << '\n';
	}
	return out.str();
}

} // namespace rexc
```

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS with diagnostics tests listed.

- [ ] **Step 5: Commit**

```sh
git add CMakeLists.txt include/rexc/source.hpp src/source.cpp include/rexc/diagnostics.hpp src/diagnostics.cpp tests/diagnostics_tests.cpp
git commit -m "feat: add source diagnostics"
```

## Task 3: ANTLR Grammar And Parse Wrapper

**Files:**
- Create: `grammar/Rexc.g4`
- Create: `cmake/FindANTLR.cmake`
- Create: `include/rexc/parse.hpp`
- Create: `src/parse.cpp`
- Create: `tests/frontend_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing parse tests**

Create `tests/frontend_tests.cpp`:

```cpp
#include "rexc/diagnostics.hpp"
#include "rexc/parse.hpp"
#include "rexc/source.hpp"

#include <functional>
#include <stdexcept>
#include <string>

void register_test(const char *name, std::function<void()> run);

#define TEST_CASE(name) \
	static void name(); \
	static const bool name##_registered = [] { \
		register_test(#name, name); \
		return true; \
	}(); \
	static void name()

#define REQUIRE(value) \
	do { \
		if (!(value)) \
			throw std::runtime_error("requirement failed: " #value); \
	} while (false)

TEST_CASE(parser_accepts_minimal_function)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(parser_rejects_malformed_function)
{
	rexc::SourceFile source("test.rx", "fn main( -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.has_errors());
}
```

Modify `CMakeLists.txt` to include `tests/frontend_tests.cpp` and `src/parse.cpp`.

- [ ] **Step 2: Run test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: FAIL because `rexc/parse.hpp` does not exist.

- [ ] **Step 3: Add ANTLR grammar and CMake integration**

Create `grammar/Rexc.g4`:

```antlr
grammar Rexc;

compilationUnit
	: item* EOF
	;

item
	: externFunction
	| functionDefinition
	;

externFunction
	: 'extern' 'fn' IDENT '(' parameterList? ')' '->' type ';'
	;

functionDefinition
	: 'fn' IDENT '(' parameterList? ')' '->' type block
	;

parameterList
	: parameter (',' parameter)*
	;

parameter
	: IDENT ':' type
	;

type
	: 'i32'
	;

block
	: '{' statement* '}'
	;

statement
	: letStatement
	| returnStatement
	;

letStatement
	: 'let' IDENT ':' type '=' expression ';'
	;

returnStatement
	: 'return' expression ';'
	;

expression
	: additive
	;

additive
	: multiplicative (('+' | '-') multiplicative)*
	;

multiplicative
	: primary (('*' | '/') primary)*
	;

primary
	: INTEGER
	| IDENT
	| callExpression
	| '(' expression ')'
	;

callExpression
	: IDENT '(' argumentList? ')'
	;

argumentList
	: expression (',' expression)*
	;

IDENT
	: [a-zA-Z_][a-zA-Z0-9_]*
	;

INTEGER
	: [0-9]+
	;

WS
	: [ \t\r\n]+ -> skip
	;

LINE_COMMENT
	: '//' ~[\r\n]* -> skip
	;
```

Create `cmake/FindANTLR.cmake`:

```cmake
find_program(ANTLR4_JAR_EXECUTABLE NAMES antlr4)
find_path(ANTLR4_RUNTIME_INCLUDE_DIR antlr4-runtime.h)
find_library(ANTLR4_RUNTIME_LIBRARY NAMES antlr4-runtime)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ANTLR
	REQUIRED_VARS ANTLR4_JAR_EXECUTABLE ANTLR4_RUNTIME_INCLUDE_DIR ANTLR4_RUNTIME_LIBRARY
)
```

Modify `CMakeLists.txt` to generate parser sources:

```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(ANTLR REQUIRED)

set(REXC_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(REXC_GENERATED_SOURCES
	"${REXC_GENERATED_DIR}/RexcLexer.cpp"
	"${REXC_GENERATED_DIR}/RexcParser.cpp"
	"${REXC_GENERATED_DIR}/RexcBaseVisitor.cpp"
	"${REXC_GENERATED_DIR}/RexcVisitor.cpp"
)

add_custom_command(
	OUTPUT ${REXC_GENERATED_SOURCES}
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${REXC_GENERATED_DIR}"
	COMMAND "${ANTLR4_JAR_EXECUTABLE}" -Dlanguage=Cpp -visitor -no-listener
	        -o "${REXC_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/grammar/Rexc.g4"
	DEPENDS grammar/Rexc.g4
	VERBATIM
)
```

Add generated include paths and runtime library to `rexc_core`.

- [ ] **Step 4: Add parse wrapper**

Create `include/rexc/parse.hpp`:

```cpp
#pragma once

#include "rexc/diagnostics.hpp"
#include "rexc/source.hpp"

#include <memory>

namespace rexc {

class ParseTreeHandle {
public:
	ParseTreeHandle();
	~ParseTreeHandle();

	ParseTreeHandle(ParseTreeHandle &&) noexcept;
	ParseTreeHandle &operator=(ParseTreeHandle &&) noexcept;

	ParseTreeHandle(const ParseTreeHandle &) = delete;
	ParseTreeHandle &operator=(const ParseTreeHandle &) = delete;

	bool ok() const;

private:
	struct Impl;
	explicit ParseTreeHandle(std::unique_ptr<Impl> impl);

	std::unique_ptr<Impl> impl_;

	friend ParseTreeHandle parse_source(const SourceFile &, Diagnostics &);
};

ParseTreeHandle parse_source(const SourceFile &source, Diagnostics &diagnostics);

} // namespace rexc
```

Create `src/parse.cpp`:

```cpp
#include "rexc/parse.hpp"

#include "RexcLexer.h"
#include "RexcParser.h"

#include <antlr4-runtime.h>

#include <memory>
#include <string>

namespace rexc {
namespace {

class DiagnosticErrorListener : public antlr4::BaseErrorListener {
public:
	DiagnosticErrorListener(const SourceFile &source, Diagnostics &diagnostics)
		: source_(source), diagnostics_(diagnostics)
	{
	}

	void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t line,
	                 size_t char_position_in_line, const std::string &msg,
	                 std::exception_ptr) override
	{
		diagnostics_.error(SourceLocation{source_.path(), 0, line, char_position_in_line + 1}, msg);
	}

private:
	const SourceFile &source_;
	Diagnostics &diagnostics_;
};

} // namespace

struct ParseTreeHandle::Impl {
	explicit Impl(bool ok_value) : ok(ok_value) {}
	bool ok;
};

ParseTreeHandle::ParseTreeHandle() = default;
ParseTreeHandle::~ParseTreeHandle() = default;
ParseTreeHandle::ParseTreeHandle(ParseTreeHandle &&) noexcept = default;
ParseTreeHandle &ParseTreeHandle::operator=(ParseTreeHandle &&) noexcept = default;

ParseTreeHandle::ParseTreeHandle(std::unique_ptr<Impl> impl)
	: impl_(std::move(impl))
{
}

bool ParseTreeHandle::ok() const
{
	return impl_ && impl_->ok;
}

ParseTreeHandle parse_source(const SourceFile &source, Diagnostics &diagnostics)
{
	antlr4::ANTLRInputStream input(source.text());
	RexcLexer lexer(&input);
	antlr4::CommonTokenStream tokens(&lexer);
	RexcParser parser(&tokens);
	DiagnosticErrorListener listener(source, diagnostics);

	lexer.removeErrorListeners();
	parser.removeErrorListeners();
	lexer.addErrorListener(&listener);
	parser.addErrorListener(&listener);

	parser.compilationUnit();
	return ParseTreeHandle(std::make_unique<ParseTreeHandle::Impl>(!diagnostics.has_errors()));
}

} // namespace rexc
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS if ANTLR and its C++ runtime are installed. If CMake cannot find ANTLR, install `antlr4` and `antlr4-cpp-runtime`, then rerun the exact commands.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt cmake/FindANTLR.cmake grammar/Rexc.g4 include/rexc/parse.hpp src/parse.cpp tests/frontend_tests.cpp
git commit -m "feat: add ANTLR parser"
```

## Task 4: Compiler-Owned AST And AST Builder

**Files:**
- Create: `include/rexc/ast.hpp`
- Create: `src/ast.cpp`
- Create: `include/rexc/ast_builder.hpp`
- Create: `src/ast_builder.cpp`
- Modify: `include/rexc/parse.hpp`
- Modify: `src/parse.cpp`
- Modify: `tests/frontend_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing AST test**

Append to `tests/frontend_tests.cpp`:

```cpp
TEST_CASE(parser_builds_ast_for_add_function)
{
	rexc::SourceFile source("test.rx",
		"fn add(a: i32, b: i32) -> i32 { return a + b; }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);
	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE(module.functions.size() == 1);
	REQUIRE(module.functions[0].name == "add");
	REQUIRE(module.functions[0].parameters.size() == 2);
	REQUIRE(module.functions[0].return_type.name == "i32");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: FAIL because `ParseTreeHandle::module()` and AST types do not exist.

- [ ] **Step 3: Add AST model**

Create `include/rexc/ast.hpp`:

```cpp
#pragma once

#include "rexc/source.hpp"

#include <memory>
#include <string>
#include <vector>

namespace rexc::ast {

struct TypeName {
	std::string name;
	SourceLocation location;
};

struct Parameter {
	std::string name;
	TypeName type;
	SourceLocation location;
};

struct Expr {
	enum class Kind { Integer, Name, Binary, Call };
	explicit Expr(Kind kind, SourceLocation location) : kind(kind), location(std::move(location)) {}
	virtual ~Expr() = default;
	Kind kind;
	SourceLocation location;
};

struct IntegerExpr final : Expr {
	IntegerExpr(SourceLocation location, int value) : Expr(Kind::Integer, std::move(location)), value(value) {}
	int value;
};

struct NameExpr final : Expr {
	NameExpr(SourceLocation location, std::string name) : Expr(Kind::Name, std::move(location)), name(std::move(name)) {}
	std::string name;
};

struct BinaryExpr final : Expr {
	BinaryExpr(SourceLocation location, std::string op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
		: Expr(Kind::Binary, std::move(location)), op(std::move(op)), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
	std::string op;
	std::unique_ptr<Expr> lhs;
	std::unique_ptr<Expr> rhs;
};

struct CallExpr final : Expr {
	CallExpr(SourceLocation location, std::string callee) : Expr(Kind::Call, std::move(location)), callee(std::move(callee)) {}
	std::string callee;
	std::vector<std::unique_ptr<Expr>> arguments;
};

struct Stmt {
	enum class Kind { Let, Return };
	explicit Stmt(Kind kind, SourceLocation location) : kind(kind), location(std::move(location)) {}
	virtual ~Stmt() = default;
	Kind kind;
	SourceLocation location;
};

struct LetStmt final : Stmt {
	LetStmt(SourceLocation location, std::string name, TypeName type, std::unique_ptr<Expr> initializer)
		: Stmt(Kind::Let, std::move(location)), name(std::move(name)), type(std::move(type)), initializer(std::move(initializer)) {}
	std::string name;
	TypeName type;
	std::unique_ptr<Expr> initializer;
};

struct ReturnStmt final : Stmt {
	ReturnStmt(SourceLocation location, std::unique_ptr<Expr> value)
		: Stmt(Kind::Return, std::move(location)), value(std::move(value)) {}
	std::unique_ptr<Expr> value;
};

struct Function {
	bool is_extern = false;
	std::string name;
	std::vector<Parameter> parameters;
	TypeName return_type;
	std::vector<std::unique_ptr<Stmt>> body;
	SourceLocation location;
};

struct Module {
	std::vector<Function> functions;
};

} // namespace rexc::ast
```

Create `src/ast.cpp`:

```cpp
#include "rexc/ast.hpp"
```

- [ ] **Step 4: Implement AST builder and parse result**

Create `include/rexc/ast_builder.hpp` with a declaration:

```cpp
#pragma once

#include "rexc/ast.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/source.hpp"

class RexcParser;

namespace rexc {

ast::Module build_ast(RexcParser::CompilationUnitContext &tree,
                      const SourceFile &source,
                      Diagnostics &diagnostics);

} // namespace rexc
```

Implement `src/ast_builder.cpp` with a visitor that creates `ast::Module`, `ast::Function`, statements, and expressions from all grammar alternatives in `Rexc.g4`.

Update `ParseTreeHandle` into a value-style `ParseResult`:

```cpp
class ParseResult {
public:
	ParseResult(bool ok, ast::Module module);
	bool ok() const;
	const ast::Module &module() const;
	ast::Module take_module();

private:
	bool ok_;
	ast::Module module_;
};

ParseResult parse_source(const SourceFile &source, Diagnostics &diagnostics);
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS including `parser_builds_ast_for_add_function`.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt include/rexc/ast.hpp src/ast.cpp include/rexc/ast_builder.hpp src/ast_builder.cpp include/rexc/parse.hpp src/parse.cpp tests/frontend_tests.cpp
git commit -m "feat: build compiler AST"
```

## Task 5: Semantic Analysis

**Files:**
- Create: `include/rexc/sema.hpp`
- Create: `src/sema.cpp`
- Create: `tests/sema_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing semantic tests**

Create `tests/sema_tests.cpp` with tests that parse source and call `rexc::analyze_module`:

```cpp
#include "rexc/diagnostics.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"

#include <functional>
#include <stdexcept>
#include <string>

void register_test(const char *name, std::function<void()> run);

#define TEST_CASE(name) \
	static void name(); \
	static const bool name##_registered = [] { \
		register_test(#name, name); \
		return true; \
	}(); \
	static void name()

#define REQUIRE(value) \
	do { \
		if (!(value)) \
			throw std::runtime_error("requirement failed: " #value); \
	} while (false)

static rexc::SemanticResult analyze(const std::string &text, rexc::Diagnostics &diagnostics)
{
	rexc::SourceFile source("test.rx", text);
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	return rexc::analyze_module(parsed.module(), diagnostics);
}

TEST_CASE(sema_accepts_valid_add_program)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn add(a: i32, b: i32) -> i32 { return a + b; }\n", diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_duplicate_functions)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { return 0; }\nfn main() -> i32 { return 1; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate function 'main'") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_call)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { return missing(); }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown function 'missing'") != std::string::npos);
}

TEST_CASE(sema_rejects_wrong_arity)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn add(a: i32) -> i32 { return a; }\nfn main() -> i32 { return add(1, 2); }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("expected 1 arguments but got 2") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: FAIL because `rexc/sema.hpp` does not exist.

- [ ] **Step 3: Implement semantic result and checks**

Create `include/rexc/sema.hpp`:

```cpp
#pragma once

#include "rexc/ast.hpp"
#include "rexc/diagnostics.hpp"

namespace rexc {

class SemanticResult {
public:
	explicit SemanticResult(bool ok) : ok_(ok) {}
	bool ok() const { return ok_; }

private:
	bool ok_;
};

SemanticResult analyze_module(const ast::Module &module, Diagnostics &diagnostics);

} // namespace rexc
```

Create `src/sema.cpp` implementing:

- module function table keyed by name;
- duplicate function rejection;
- per-function local table seeded with parameters;
- duplicate local rejection;
- expression checking for integer literals, names, binary expressions, and calls;
- return expression type check against the declared return type.

All valid expressions return type name `"i32"` in this milestone.

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS including semantic tests.

- [ ] **Step 5: Commit**

```sh
git add CMakeLists.txt include/rexc/sema.hpp src/sema.cpp tests/sema_tests.cpp
git commit -m "feat: add semantic analysis"
```

## Task 6: Typed IR And Lowering

**Files:**
- Create: `include/rexc/ir.hpp`
- Create: `src/ir.cpp`
- Create: `include/rexc/lower_ir.hpp`
- Create: `src/lower_ir.cpp`
- Create: `tests/ir_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing IR tests**

Create `tests/ir_tests.cpp`:

```cpp
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"

#include <functional>
#include <stdexcept>
#include <string>

void register_test(const char *name, std::function<void()> run);

#define TEST_CASE(name) \
	static void name(); \
	static const bool name##_registered = [] { \
		register_test(#name, name); \
		return true; \
	}(); \
	static void name()

#define REQUIRE(value) \
	do { \
		if (!(value)) \
			throw std::runtime_error("requirement failed: " #value); \
	} while (false)

TEST_CASE(lowering_preserves_function_signature)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 42; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	auto module = rexc::lower_to_ir(parsed.module());
	REQUIRE(module.functions.size() == 1);
	REQUIRE(module.functions[0].name == "main");
	REQUIRE(module.functions[0].return_type == rexc::ir::Type::I32);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: FAIL because `rexc/lower_ir.hpp` does not exist.

- [ ] **Step 3: Implement IR**

Create `include/rexc/ir.hpp` with:

```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace rexc::ir {

enum class Type { I32 };

struct Value {
	enum class Kind { Integer, Local, Binary, Call };
	explicit Value(Kind kind, Type type) : kind(kind), type(type) {}
	virtual ~Value() = default;
	Kind kind;
	Type type;
};

struct IntegerValue final : Value {
	explicit IntegerValue(int value) : Value(Kind::Integer, Type::I32), value(value) {}
	int value;
};

struct LocalValue final : Value {
	explicit LocalValue(std::string name) : Value(Kind::Local, Type::I32), name(std::move(name)) {}
	std::string name;
};

struct BinaryValue final : Value {
	BinaryValue(std::string op, std::unique_ptr<Value> lhs, std::unique_ptr<Value> rhs)
		: Value(Kind::Binary, Type::I32), op(std::move(op)), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
	std::string op;
	std::unique_ptr<Value> lhs;
	std::unique_ptr<Value> rhs;
};

struct CallValue final : Value {
	explicit CallValue(std::string callee) : Value(Kind::Call, Type::I32), callee(std::move(callee)) {}
	std::string callee;
	std::vector<std::unique_ptr<Value>> arguments;
};

struct Statement {
	enum class Kind { Let, Return };
	explicit Statement(Kind kind) : kind(kind) {}
	virtual ~Statement() = default;
	Kind kind;
};

struct LetStatement final : Statement {
	LetStatement(std::string name, std::unique_ptr<Value> value)
		: Statement(Kind::Let), name(std::move(name)), value(std::move(value)) {}
	std::string name;
	std::unique_ptr<Value> value;
};

struct ReturnStatement final : Statement {
	explicit ReturnStatement(std::unique_ptr<Value> value)
		: Statement(Kind::Return), value(std::move(value)) {}
	std::unique_ptr<Value> value;
};

struct Parameter {
	std::string name;
	Type type = Type::I32;
};

struct Function {
	bool is_extern = false;
	std::string name;
	std::vector<Parameter> parameters;
	Type return_type = Type::I32;
	std::vector<std::unique_ptr<Statement>> body;
};

struct Module {
	std::vector<Function> functions;
};

} // namespace rexc::ir
```

Create `src/ir.cpp`:

```cpp
#include "rexc/ir.hpp"
```

- [ ] **Step 4: Implement lowering**

Create `include/rexc/lower_ir.hpp`:

```cpp
#pragma once

#include "rexc/ast.hpp"
#include "rexc/ir.hpp"

namespace rexc {

ir::Module lower_to_ir(const ast::Module &module);

} // namespace rexc
```

Create `src/lower_ir.cpp` to recursively lower all AST functions, statements,
and expressions into equivalent IR nodes.

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS including IR tests.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt include/rexc/ir.hpp src/ir.cpp include/rexc/lower_ir.hpp src/lower_ir.cpp tests/ir_tests.cpp
git commit -m "feat: add typed IR lowering"
```

## Task 7: i386 Assembly Codegen

**Files:**
- Create: `include/rexc/codegen_x86.hpp`
- Create: `src/codegen_x86.cpp`
- Create: `tests/codegen_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing codegen test**

Create `tests/codegen_tests.cpp`:

```cpp
#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"

#include <functional>
#include <stdexcept>
#include <string>

void register_test(const char *name, std::function<void()> run);

#define TEST_CASE(name) \
	static void name(); \
	static const bool name##_registered = [] { \
		register_test(#name, name); \
		return true; \
	}(); \
	static void name()

#define REQUIRE(value) \
	do { \
		if (!(value)) \
			throw std::runtime_error("requirement failed: " #value); \
	} while (false)

TEST_CASE(codegen_emits_main_returning_integer)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 42; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	auto module = rexc::lower_to_ir(parsed.module());
	auto assembly = rexc::emit_x86_assembly(module);
	REQUIRE(assembly.find(".globl main") != std::string::npos);
	REQUIRE(assembly.find("main:") != std::string::npos);
	REQUIRE(assembly.find("movl $42, %eax") != std::string::npos);
	REQUIRE(assembly.find("leave") != std::string::npos);
	REQUIRE(assembly.find("ret") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: FAIL because `rexc/codegen_x86.hpp` does not exist.

- [ ] **Step 3: Implement assembly emitter**

Create `include/rexc/codegen_x86.hpp`:

```cpp
#pragma once

#include "rexc/ir.hpp"

#include <string>

namespace rexc {

std::string emit_x86_assembly(const ir::Module &module);

} // namespace rexc
```

Create `src/codegen_x86.cpp` implementing:

- `.text` section;
- no body emitted for extern functions;
- `.globl name` for defined functions;
- stack frame prologue and epilogue;
- argument offsets at `8(%ebp)`, `12(%ebp)`, and so on;
- local stack slots below `%ebp`;
- expression codegen into `%eax`;
- binary expression evaluation using `%eax` and stack save/restore;
- cdecl calls with caller stack cleanup.

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS including codegen tests.

- [ ] **Step 5: Commit**

```sh
git add CMakeLists.txt include/rexc/codegen_x86.hpp src/codegen_x86.cpp tests/codegen_tests.cpp
git commit -m "feat: emit i386 assembly"
```

## Task 8: CLI And Example Program

**Files:**
- Create: `src/main.cpp`
- Create: `examples/add.rx`
- Create: `tests/cli_smoke.sh`
- Modify: `CMakeLists.txt`
- Modify: `README.md`

- [ ] **Step 1: Write failing CLI smoke test**

Create `tests/cli_smoke.sh`:

```sh
#!/bin/sh
set -eu

build_dir=$1
tmp_dir="${build_dir}/cli-smoke"
mkdir -p "$tmp_dir"

"${build_dir}/rexc" "${PWD}/examples/add.rx" -S -o "${tmp_dir}/add.s"
test -s "${tmp_dir}/add.s"
grep -q ".globl main" "${tmp_dir}/add.s"
grep -q "call add" "${tmp_dir}/add.s"
```

Make it executable:

```sh
chmod +x tests/cli_smoke.sh
```

Create `examples/add.rx`:

```rust
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    let value: i32 = add(20, 22);
    return value;
}
```

Modify `CMakeLists.txt` to add a `rexc` executable from `src/main.cpp` and a CTest:

```cmake
add_executable(rexc src/main.cpp)
target_link_libraries(rexc PRIVATE rexc_core)

add_test(NAME cli_smoke COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.sh" "${CMAKE_CURRENT_BINARY_DIR}")
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: FAIL because `src/main.cpp` does not exist.

- [ ] **Step 3: Implement CLI**

Create `src/main.cpp`:

```cpp
#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::string &path)
{
	std::ifstream input(path);
	if (!input)
		throw std::runtime_error("failed to open input file: " + path);
	std::ostringstream buffer;
	buffer << input.rdbuf();
	return buffer.str();
}

void write_file(const std::string &path, const std::string &text)
{
	std::ofstream output(path);
	if (!output)
		throw std::runtime_error("failed to open output file: " + path);
	output << text;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 5 || std::string(argv[2]) != "-S" || std::string(argv[3]) != "-o") {
		std::cerr << "usage: rexc input.rx -S -o output.s\n";
		return 2;
	}

	const std::string input_path = argv[1];
	const std::string output_path = argv[4];

	try {
		rexc::SourceFile source(input_path, read_file(input_path));
		rexc::Diagnostics diagnostics;
		auto parsed = rexc::parse_source(source, diagnostics);
		if (!parsed.ok()) {
			std::cerr << diagnostics.format();
			return 1;
		}

		auto sema = rexc::analyze_module(parsed.module(), diagnostics);
		if (!sema.ok()) {
			std::cerr << diagnostics.format();
			return 1;
		}

		auto ir = rexc::lower_to_ir(parsed.module());
		write_file(output_path, rexc::emit_x86_assembly(ir));
		return 0;
	} catch (const std::exception &err) {
		std::cerr << "rexc: " << err.what() << "\n";
		return 1;
	}
}
```

- [ ] **Step 4: Update README usage**

Append to `README.md`:

```markdown
## Compile To Assembly

```sh
build/rexc examples/add.rx -S -o build/add.s
```

The generated assembly is GNU assembler-compatible 32-bit x86 text intended for
the Drunix userland toolchain.
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for unit tests and `cli_smoke`.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt README.md src/main.cpp examples/add.rx tests/cli_smoke.sh
git commit -m "feat: add compiler CLI"
```

## Task 9: Assembly Toolchain Smoke Check

**Files:**
- Create: `tests/assemble_smoke.sh`
- Modify: `CMakeLists.txt`
- Modify: `README.md`

- [ ] **Step 1: Write failing assemble smoke test**

Create `tests/assemble_smoke.sh`:

```sh
#!/bin/sh
set -eu

build_dir=$1
tmp_dir="${build_dir}/assemble-smoke"
mkdir -p "$tmp_dir"

"${build_dir}/rexc" "${PWD}/examples/add.rx" -S -o "${tmp_dir}/add.s"

if command -v x86_64-elf-as >/dev/null 2>&1; then
	x86_64-elf-as --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
elif command -v as >/dev/null 2>&1; then
	as --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
else
	echo "SKIP: no GNU assembler found"
	exit 0
fi

test -s "${tmp_dir}/add.o"
```

Make it executable:

```sh
chmod +x tests/assemble_smoke.sh
```

Modify `CMakeLists.txt`:

```cmake
add_test(NAME assemble_smoke COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/assemble_smoke.sh" "${CMAKE_CURRENT_BINARY_DIR}")
```

- [ ] **Step 2: Run test to verify current behavior**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS if assembler accepts the emitted syntax, or SKIP message with exit 0 if no assembler is available.

- [ ] **Step 3: Fix emitted assembly only if assembler rejects it**

If `assemble_smoke` fails with assembler syntax errors, update `src/codegen_x86.cpp` to emit valid GNU assembler i386 syntax. Keep the unit test golden fragments aligned with the corrected syntax.

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for all tests.

- [ ] **Step 5: Update README dependency notes**

Add:

```markdown
## Optional Assembly Check

`ctest` runs an assembler smoke check when `x86_64-elf-as` or GNU `as` is
available. The check compiles `examples/add.rx` to assembly and assembles it
as a 32-bit x86 object file.
```

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt README.md tests/assemble_smoke.sh src/codegen_x86.cpp tests/codegen_tests.cpp
git commit -m "test: add assembler smoke check"
```

## Task 10: Final Verification

**Files:**
- Modify only if verification exposes a bug.

- [ ] **Step 1: Run clean configure**

Run:

```sh
rm -rf build
cmake -S . -B build
```

Expected: configure succeeds and reports C++ compiler and ANTLR/runtime discovery.

- [ ] **Step 2: Run clean build**

Run:

```sh
cmake --build build
```

Expected: build succeeds with `rexc` and `rexc_tests`.

- [ ] **Step 3: Run all tests**

Run:

```sh
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Manually inspect generated assembly**

Run:

```sh
build/rexc examples/add.rx -S -o build/add.s
sed -n '1,160p' build/add.s
```

Expected: output contains `.text`, `add:`, `main:`, stack prologues, `call add`, and `ret`.

- [ ] **Step 5: Check git status**

Run:

```sh
git status --short
```

Expected: clean worktree after final commit, or only intentional uncommitted changes that are documented before handoff.
