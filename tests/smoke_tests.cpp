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

	// The mangled `_arena_*` symbols must be referenced from user main.
	// (The bodies themselves live in the separately-emitted stdlib
	// assembly that the CLI concatenates at link time; the user-module
	// assembly only carries the call sites — which is what proves
	// sema + lower + codegen wired the references through.)
	REQUIRE(assembly.find("bl _arena_init") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_alloc") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_used") != std::string::npos);
}

// FE-106: round-trip smoke under the CLI's *default* symbol policy
// (StdlibSymbolPolicy::DefaultPrelude). User code must reach both the
// implicit-static `alloc_bytes` helper and the explicit-arena
// `arena_*` API by bare name, and must be able to name `Arena` as a
// type — all without enabling StdlibSymbolPolicy::All.
//
// This is the user-visibility complement to FE-105: that ticket made
// the symbols exist and resolve under the All policy; this ticket
// lifts them into the default prelude so the CLI works out of the
// box.
TEST_CASE(smoke_arena_and_alloc_under_default_prelude)
{
	const std::string source =
		"static mut USER_BUF: [u8; 64];\n"
		"fn main() -> i32 {\n"
		"    let legacy: *u8 = alloc_bytes(8);\n"
		"    let mut a: Arena = Arena { storage: USER_BUF + 0, capacity: 64, offset: 0 };\n"
		"    arena_init(&a, USER_BUF + 0, 64);\n"
		"    let block: *u8 = arena_alloc(&a, 12);\n"
		"    return arena_used(&a);\n"
		"}\n";

	rexc::SourceFile file("smoke_fe106.rx", source);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(file, diagnostics);
	REQUIRE(parsed.ok());

	rexc::SemanticOptions semantic_options;
	// Explicitly assert the default — this is the contract that breaks
	// before FE-106 and passes after.
	semantic_options.stdlib_symbols = rexc::StdlibSymbolPolicy::DefaultPrelude;
	semantic_options.enforce_unsafe_blocks = false;
	auto sema = rexc::analyze_module(parsed.module(), diagnostics, semantic_options);
	REQUIRE(sema.ok());
	REQUIRE(!diagnostics.has_errors());

	rexc::LowerOptions lower_options;
	lower_options.stdlib_symbols = rexc::LowerStdlibSymbolPolicy::DefaultPrelude;
	auto ir = rexc::lower_to_ir(parsed.module(), lower_options);
	auto result = rexc::emit_arm64_macos_assembly(ir, diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());

	const std::string assembly = result.assembly();

	// Both call paths must land in the emitted user-module assembly.
	REQUIRE(assembly.find("bl _alloc_bytes") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_init") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_alloc") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_used") != std::string::npos);
}

// FE-105 closure: the `Vec::with_alloc(arena)` pattern named in the
// FE-105 exit test.
//
// The verification gap flagged in commit `5411799` was that the
// pattern wording — "Vec::with_alloc(arena) pattern works" — was not
// directly demonstrated. This smoke closes that gap: a vector type
// (`ArenaVec_i32`) with a constructor (`arena_vec_i32_with_alloc`)
// that takes `*Arena` as an explicit parameter, allocates both its
// header and its backing storage from the supplied arena via
// `arena_alloc`, and supports `push` + `get` round-trip.
//
// We compile under `StdlibSymbolPolicy::DefaultPrelude` and
// `enforce_unsafe_blocks = true` — the exact policy the CLI uses —
// to prove the demo also works under default user-facing settings.
//
// This is an assembly-content smoke (no execute). The accompanying
// fixture `examples/arena_vec_demo.rx` is the same source the CLI
// exercises end-to-end; the `assemble_smoke.sh` shell harness
// extends that to assemble + link + run with `exit 60` (10+20+30).
//
// This is NOT FE-109a (real generic `Vec<T>` parameterized by
// allocator). It is the verification fixture for the FE-105 exit
// test wording.
TEST_CASE(smoke_vec_with_alloc_round_trip)
{
	const std::string source =
		"struct ArenaVec_i32 {\n"
		"    data: *i32,\n"
		"    len: i32,\n"
		"    capacity: i32,\n"
		"}\n"
		"unsafe fn arena_vec_i32_with_alloc(arena: *Arena, capacity: i32) -> *ArenaVec_i32 {\n"
		"    let header_bytes: *u8 = arena_alloc(arena, 16);\n"
		"    let data_bytes: *u8 = arena_alloc(arena, capacity * 4);\n"
		"    let v: *ArenaVec_i32 = header_bytes as *ArenaVec_i32;\n"
		"    (*v).data = data_bytes as *i32;\n"
		"    (*v).len = 0;\n"
		"    (*v).capacity = capacity;\n"
		"    return v;\n"
		"}\n"
		"unsafe fn arena_vec_i32_push(v: *ArenaVec_i32, value: i32) -> i32 {\n"
		"    if (*v).len >= (*v).capacity { return 0; }\n"
		"    let slot: *i32 = (*v).data + (*v).len;\n"
		"    *slot = value;\n"
		"    (*v).len = (*v).len + 1;\n"
		"    return 1;\n"
		"}\n"
		"unsafe fn arena_vec_i32_get(v: *ArenaVec_i32, index: i32) -> i32 {\n"
		"    return *((*v).data + index);\n"
		"}\n"
		"static mut USER_BUF: [u8; 256];\n"
		"unsafe fn main() -> i32 {\n"
		"    let mut a: Arena = Arena { storage: USER_BUF + 0, capacity: 256, offset: 0 };\n"
		"    arena_init(&a, USER_BUF + 0, 256);\n"
		"    let v: *ArenaVec_i32 = arena_vec_i32_with_alloc(&a, 4);\n"
		"    arena_vec_i32_push(v, 10);\n"
		"    arena_vec_i32_push(v, 20);\n"
		"    arena_vec_i32_push(v, 30);\n"
		"    return arena_vec_i32_get(v, 0)\n"
		"         + arena_vec_i32_get(v, 1)\n"
		"         + arena_vec_i32_get(v, 2);\n"
		"}\n";

	rexc::SourceFile file("smoke_arena_vec.rx", source);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(file, diagnostics);
	REQUIRE(parsed.ok());

	rexc::SemanticOptions semantic_options;
	// Match the CLI's user-facing defaults exactly.
	semantic_options.stdlib_symbols = rexc::StdlibSymbolPolicy::DefaultPrelude;
	semantic_options.enforce_unsafe_blocks = true;
	auto sema = rexc::analyze_module(parsed.module(), diagnostics, semantic_options);
	REQUIRE(sema.ok());
	REQUIRE(!diagnostics.has_errors());

	rexc::LowerOptions lower_options;
	lower_options.stdlib_symbols = rexc::LowerStdlibSymbolPolicy::DefaultPrelude;
	auto ir = rexc::lower_to_ir(parsed.module(), lower_options);
	auto result = rexc::emit_arm64_macos_assembly(ir, diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());

	const std::string assembly = result.assembly();

	// User module's `_main` and the static buffer it owns.
	REQUIRE(assembly.find("_main:") != std::string::npos);
	REQUIRE(assembly.find("Lstatic_USER_BUF") != std::string::npos);

	// The vector functions must be defined in the user module.
	REQUIRE(assembly.find("_arena_vec_i32_with_alloc:") != std::string::npos);
	REQUIRE(assembly.find("_arena_vec_i32_push:") != std::string::npos);
	REQUIRE(assembly.find("_arena_vec_i32_get:") != std::string::npos);

	// Main must call the constructor (with the *Arena), and the
	// constructor in turn must reach `arena_alloc` — that is the FE-105
	// pattern under test.
	REQUIRE(assembly.find("bl _arena_init") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_alloc") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_vec_i32_with_alloc") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_vec_i32_push") != std::string::npos);
	REQUIRE(assembly.find("bl _arena_vec_i32_get") != std::string::npos);
}
