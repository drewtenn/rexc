// Basic test-runner smoke coverage.
//
// This file exists to prove the lightweight in-repo test harness can register
// and execute a test before the more specific compiler suites rely on it.
#include "rexc/codegen_arm64.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <string>

TEST_CASE(smoke_test_runner_executes)
{
	REQUIRE_EQ(std::string("rexc"), std::string("rexc"));
}

// FE-105: end-to-end smoke for the arena-as-value allocation pattern.
//
// Demonstrates the FE-105 exit-test contract: a user program declares a
// static buffer, initializes an Arena value over it, allocates 12 bytes
// via `arena_alloc`, and `main` returns the arena's `offset` (which must
// be 12 — the bytes just allocated). This proves the explicit-arena
// path threads end-to-end through parse / sema / lower / codegen, with
// the implicit static `ALLOC_ARENA` allocator entirely uninvolved.
//
// The smoke runs under `StdlibSymbolPolicy::All` and
// `LowerStdlibSymbolPolicy::All` because the default-prelude policy
// does not yet expose `arena_*` to user code. FE-106 will decide
// between adding `std_arena_*` wrappers, prelude registration, or a
// `use std::alloc::*` import syntax.
//
// This is an assembly-content smoke (not a binary-execution smoke):
// we compile the Rexy program to arm64-macos assembly and assert that
// the user's `_main`, `_arena_init`, and `_arena_alloc` are all wired
// in. A follow-up could extend this to actually assemble + link + run
// once a Catch2-friendly helper for that exists.
TEST_CASE(smoke_arena_as_value_end_to_end_assembly)
{
	const std::string source =
		"static mut USER_BUF: [u8; 64];\n"
		"fn main() -> i32 {\n"
		"    let mut a: Arena = Arena { storage: USER_BUF + 0, capacity: 64, offset: 0 };\n"
		"    arena_init(&a, USER_BUF + 0, 64);\n"
		"    let block: *u8 = arena_alloc(&a, 12);\n"
		"    return arena_used(&a);\n"
		"}\n";

	rexc::SourceFile file("smoke_arena.rx", source);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(file, diagnostics);
	REQUIRE(parsed.ok());

	rexc::SemanticOptions semantic_options;
	semantic_options.stdlib_symbols = rexc::StdlibSymbolPolicy::All;
	semantic_options.enforce_unsafe_blocks = false;
	auto sema = rexc::analyze_module(parsed.module(), diagnostics, semantic_options);
	REQUIRE(sema.ok());
	REQUIRE(!diagnostics.has_errors());

	rexc::LowerOptions lower_options;
	lower_options.stdlib_symbols = rexc::LowerStdlibSymbolPolicy::All;
	auto ir = rexc::lower_to_ir(parsed.module(), lower_options);
	auto result = rexc::emit_arm64_macos_assembly(ir, diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());

	const std::string assembly = result.assembly();

	// User program's main and the static buffer it allocates from.
	REQUIRE(assembly.find("_main:") != std::string::npos);
	REQUIRE(assembly.find("Lstatic_USER_BUF") != std::string::npos);

	// The Arena explicit-allocator API must be linked in by lowering.
	REQUIRE(assembly.find("_arena_init:") != std::string::npos);
	REQUIRE(assembly.find("_arena_alloc:") != std::string::npos);
	REQUIRE(assembly.find("_arena_used:") != std::string::npos);

	// And user main must actually call into them (proving the call sites
	// went through sema + lower + codegen, not just that the helpers got
	// emitted as dead code).
	REQUIRE(assembly.find("bl _arena_init") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_alloc") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_used") != std::string::npos);
}
