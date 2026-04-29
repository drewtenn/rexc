// IR lowering tests that check typed values survive frontend stages.
//
// These tests run source through parsing and sema before lowering, then inspect
// the backend-facing IR. They catch mismatches between the AST/sema contract and
// what codegen expects to receive.
#include "rexc/ast.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <memory>
#include <stdexcept>
#include <string>

TEST_CASE(lowering_preserves_function_signature)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 42; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions.size(), 1u);
	REQUIRE_EQ(module.functions[0].name, std::string("main"));
	REQUIRE_EQ(rexc::format_type(module.functions[0].return_type), std::string("i32"));
	REQUIRE_EQ(module.functions[0].body.size(), 1u);
}

TEST_CASE(lowering_mangles_user_module_function_symbols)
{
	rexc::SourceFile source(
	    "test.rx",
	    "mod math { pub fn add(a: i32, b: i32) -> i32 { return a + b; } }\n"
	    "fn main() -> i32 { return math::add(1, 2); }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions.size(), 2u);
	REQUIRE_EQ(module.functions[0].name, std::string("math_add"));
	REQUIRE_EQ(module.functions[1].name, std::string("main"));

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[1].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Call);
	const auto &call = static_cast<const rexc::ir::CallValue &>(*ret.value);
	REQUIRE_EQ(call.callee, std::string("math_add"));
}

TEST_CASE(lowering_preserves_static_byte_buffers)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static mut USER_BUFFER: [u8; 1024];\nfn main() -> str { return USER_BUFFER; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.static_buffers.size(), std::size_t(1));
	REQUIRE_EQ(module.static_buffers[0].name, std::string("USER_BUFFER"));
	REQUIRE_EQ(rexc::format_type(module.static_buffers[0].element_type), std::string("u8"));
	REQUIRE_EQ(module.static_buffers[0].length, std::size_t(1024));
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Global);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("str"));
}

TEST_CASE(lowering_preserves_initialized_static_arrays)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static MONTHS: [str; 3] = [\"Jan\", \"Feb\", \"Mar\"];\n"
	    "fn main() -> str { return MONTHS[1]; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.static_buffers.size(), std::size_t(1));
	REQUIRE_EQ(module.static_buffers[0].name, std::string("MONTHS"));
	REQUIRE_EQ(rexc::format_type(module.static_buffers[0].element_type), std::string("str"));
	REQUIRE_EQ(module.static_buffers[0].length, std::size_t(3));
	REQUIRE_EQ(module.static_buffers[0].initializers.size(), std::size_t(3));
	REQUIRE_EQ(module.static_buffers[0].initializers[1].literal, std::string("Feb"));
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("str"));
}

TEST_CASE(lowering_preserves_static_i32_scalars)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static mut USER_COUNTER: i32 = 0;\n"
	    "fn bump() -> i32 { USER_COUNTER = USER_COUNTER + 1; return USER_COUNTER; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.static_scalars.size(), std::size_t(1));
	REQUIRE_EQ(module.static_scalars[0].name, std::string("USER_COUNTER"));
	REQUIRE_EQ(rexc::format_type(module.static_scalars[0].type), std::string("i32"));
	REQUIRE_EQ(module.static_scalars[0].initializer_literal, std::string("0"));
	REQUIRE_EQ(module.functions[0].body[0]->kind, rexc::ir::Statement::Kind::Assign);
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[1]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Global);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("i32"));
}

TEST_CASE(lowering_preserves_static_i32_buffers)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static mut OFFSETS: [i32; 8];\nfn main() -> *i32 { return OFFSETS + 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.static_buffers.size(), std::size_t(1));
	REQUIRE_EQ(module.static_buffers[0].name, std::string("OFFSETS"));
	REQUIRE_EQ(rexc::format_type(module.static_buffers[0].element_type), std::string("i32"));
	REQUIRE_EQ(module.static_buffers[0].length, std::size_t(8));
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("*i32"));
}

TEST_CASE(lowering_lowers_unary_minus_as_typed_unary_value)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return -1; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Unary);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("i32"));
	const auto &unary = static_cast<const rexc::ir::UnaryValue &>(*ret.value);
	REQUIRE_EQ(unary.op, std::string("-"));
	REQUIRE_EQ(unary.operand->kind, rexc::ir::Value::Kind::Integer);
	const auto &integer = static_cast<const rexc::ir::IntegerValue &>(*unary.operand);
	REQUIRE_EQ(integer.literal, std::string("1"));
	REQUIRE(!integer.is_negative);
	REQUIRE_EQ(rexc::format_type(integer.type), std::string("i32"));
}

TEST_CASE(lowering_allows_i32_min_integer_literal)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { let x: i32 = -2147483648; return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	(void)rexc::lower_to_ir(parsed.module());
}

TEST_CASE(lowering_preserves_non_i32_types_and_literals)
{
	rexc::SourceFile source(
		"test.rx",
		"fn main() -> u32 { let ok: bool = true; let c: char = 'x'; let s: str = \"hi\"; let n: u32 = 42; return n; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions.size(), 1u);
	const auto &function = module.functions[0];
	REQUIRE_EQ(rexc::format_type(function.return_type), std::string("u32"));
	REQUIRE_EQ(function.body.size(), 5u);

	const auto &ok = static_cast<const rexc::ir::LetStatement &>(*function.body[0]);
	REQUIRE_EQ(ok.value->kind, rexc::ir::Value::Kind::Bool);
	REQUIRE_EQ(rexc::format_type(ok.value->type), std::string("bool"));
	const auto &ok_value = static_cast<const rexc::ir::BoolValue &>(*ok.value);
	REQUIRE(ok_value.value);

	const auto &c = static_cast<const rexc::ir::LetStatement &>(*function.body[1]);
	REQUIRE_EQ(c.value->kind, rexc::ir::Value::Kind::Char);
	REQUIRE_EQ(rexc::format_type(c.value->type), std::string("char"));
	const auto &char_value = static_cast<const rexc::ir::CharValue &>(*c.value);
	REQUIRE_EQ(char_value.value, U'x');

	const auto &s = static_cast<const rexc::ir::LetStatement &>(*function.body[2]);
	REQUIRE_EQ(s.value->kind, rexc::ir::Value::Kind::String);
	REQUIRE_EQ(rexc::format_type(s.value->type), std::string("str"));
	const auto &string_value = static_cast<const rexc::ir::StringValue &>(*s.value);
	REQUIRE_EQ(string_value.value, std::string("hi"));

	const auto &n = static_cast<const rexc::ir::LetStatement &>(*function.body[3]);
	REQUIRE_EQ(n.value->kind, rexc::ir::Value::Kind::Integer);
	REQUIRE_EQ(rexc::format_type(n.value->type), std::string("u32"));
	const auto &integer_value = static_cast<const rexc::ir::IntegerValue &>(*n.value);
	REQUIRE_EQ(integer_value.literal, std::string("42"));
	REQUIRE(!integer_value.is_negative);

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*function.body[4]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Local);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("u32"));
}

TEST_CASE(lowering_preserves_non_i32_parameter_types)
{
	rexc::SourceFile source("test.rx", "fn id(x: u64, ok: bool) -> u64 { return x; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions.size(), 1u);
	const auto &function = module.functions[0];
	REQUIRE_EQ(rexc::format_type(function.return_type), std::string("u64"));
	REQUIRE_EQ(function.parameters.size(), 2u);
	REQUIRE_EQ(function.parameters[0].name, std::string("x"));
	REQUIRE_EQ(rexc::format_type(function.parameters[0].type), std::string("u64"));
	REQUIRE_EQ(function.parameters[1].name, std::string("ok"));
	REQUIRE_EQ(rexc::format_type(function.parameters[1].type), std::string("bool"));

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*function.body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Local);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("u64"));
}

TEST_CASE(lowering_preserves_integer_literal_text_that_exceeds_i32_storage)
{
	rexc::SourceFile source(
		"test.rx",
		"fn main() -> i32 { let max: u64 = 18446744073709551615; let min: i64 = -9223372036854775808; return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const auto &max = static_cast<const rexc::ir::LetStatement &>(*module.functions[0].body[0]);
	const auto &max_value = static_cast<const rexc::ir::IntegerValue &>(*max.value);
	REQUIRE_EQ(rexc::format_type(max_value.type), std::string("u64"));
	REQUIRE_EQ(max_value.literal, std::string("18446744073709551615"));
	REQUIRE(!max_value.is_negative);

	const auto &min = static_cast<const rexc::ir::LetStatement &>(*module.functions[0].body[1]);
	REQUIRE_EQ(min.value->kind, rexc::ir::Value::Kind::Unary);
	REQUIRE_EQ(rexc::format_type(min.value->type), std::string("i64"));
	const auto &min_value = static_cast<const rexc::ir::UnaryValue &>(*min.value);
	REQUIRE_EQ(min_value.op, std::string("-"));
	REQUIRE_EQ(min_value.operand->kind, rexc::ir::Value::Kind::Integer);
	const auto &min_integer = static_cast<const rexc::ir::IntegerValue &>(*min_value.operand);
	REQUIRE_EQ(rexc::format_type(min_integer.type), std::string("i64"));
	REQUIRE_EQ(min_integer.literal, std::string("9223372036854775808"));
	REQUIRE(!min_integer.is_negative);
}

TEST_CASE(lowering_rejects_invalid_type_names)
{
	rexc::SourceLocation location{"test.rx", 0, 1, 1};
	rexc::ast::Module module;
	rexc::ast::Function function;
	function.name = "main";
	function.return_type = {"mystery", location};
	function.location = location;
	function.body.push_back(std::make_unique<rexc::ast::ReturnStmt>(
		location, std::make_unique<rexc::ast::IntegerExpr>(location, 1, "1")));
	module.functions.push_back(std::move(function));

	try {
		(void)rexc::lower_to_ir(module);
		REQUIRE(false);
	} catch (const std::runtime_error &err) {
		REQUIRE(std::string(err.what()).find("unknown primitive type in IR lowering: mystery") !=
		        std::string::npos);
	}
}

TEST_CASE(lowering_lowers_comparison_as_bool_value)
{
	rexc::SourceFile source("test.rx", "fn main() -> bool { return 1 >= 2; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Binary);
	REQUIRE_EQ(ret.value->type, rexc::PrimitiveType{rexc::PrimitiveKind::Bool});
	const auto &binary = static_cast<const rexc::ir::BinaryValue &>(*ret.value);
	rexc::PrimitiveType i32_type{rexc::PrimitiveKind::SignedInteger, 32};
	REQUIRE_EQ(binary.op, std::string(">="));
	REQUIRE_EQ(binary.lhs->type, i32_type);
	REQUIRE_EQ(binary.rhs->type, i32_type);
}

TEST_CASE(lowering_lowers_boolean_operators_as_bool_values)
{
	rexc::SourceFile source(
	    "test.rx", "fn main() -> bool { return !false || true && false; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Binary);
	REQUIRE_EQ(ret.value->type, rexc::PrimitiveType{rexc::PrimitiveKind::Bool});
	const auto &logical_or = static_cast<const rexc::ir::BinaryValue &>(*ret.value);
	REQUIRE_EQ(logical_or.op, std::string("||"));
	REQUIRE_EQ(logical_or.lhs->kind, rexc::ir::Value::Kind::Unary);
	const auto &logical_not = static_cast<const rexc::ir::UnaryValue &>(*logical_or.lhs);
	REQUIRE_EQ(logical_not.op, std::string("!"));
	REQUIRE_EQ(logical_not.type, rexc::PrimitiveType{rexc::PrimitiveKind::Bool});
	REQUIRE_EQ(logical_or.rhs->kind, rexc::ir::Value::Kind::Binary);
	const auto &logical_and = static_cast<const rexc::ir::BinaryValue &>(*logical_or.rhs);
	REQUIRE_EQ(logical_and.op, std::string("&&"));
	REQUIRE_EQ(logical_and.type, rexc::PrimitiveType{rexc::PrimitiveKind::Bool});
}

TEST_CASE(lowering_lowers_cast_expression)
{
	rexc::SourceFile source("test.rx", "fn main() -> u32 { return 'A' as u32; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Cast);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("u32"));
	const auto &cast = static_cast<const rexc::ir::CastValue &>(*ret.value);
	REQUIRE_EQ(cast.value->kind, rexc::ir::Value::Kind::Char);
	REQUIRE_EQ(rexc::format_type(cast.value->type), std::string("char"));
}

TEST_CASE(lowering_lowers_u8_pointer_to_str_cast)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static mut BUFFER: [u8; 16];\nfn main() -> str { return (BUFFER + 0) as str; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Cast);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("str"));
	const auto &cast = static_cast<const rexc::ir::CastValue &>(*ret.value);
	REQUIRE_EQ(rexc::format_type(cast.value->type), std::string("*u8"));
}

TEST_CASE(lowering_lowers_pointer_to_pointer_cast)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static mut BUFFER: [u8; 16];\nfn main() -> *i32 { return (BUFFER + 0) as *i32; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Cast);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("*i32"));
	const auto &cast = static_cast<const rexc::ir::CastValue &>(*ret.value);
	REQUIRE_EQ(rexc::format_type(cast.value->type), std::string("*u8"));
}

TEST_CASE(lowering_lowers_pointer_address_deref_and_indirect_assignment)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; *p = 9; return *p; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &function = module.functions[0];
	REQUIRE_EQ(function.body.size(), std::size_t(4));

	const auto &pointer = static_cast<const rexc::ir::LetStatement &>(*function.body[1]);
	REQUIRE_EQ(rexc::format_type(pointer.value->type), std::string("*i32"));
	REQUIRE_EQ(pointer.value->kind, rexc::ir::Value::Kind::Unary);
	const auto &address = static_cast<const rexc::ir::UnaryValue &>(*pointer.value);
	REQUIRE_EQ(address.op, std::string("&"));
	REQUIRE_EQ(address.operand->kind, rexc::ir::Value::Kind::Local);

	REQUIRE_EQ(function.body[2]->kind, rexc::ir::Statement::Kind::IndirectAssign);
	const auto &assign =
	    static_cast<const rexc::ir::IndirectAssignStatement &>(*function.body[2]);
	REQUIRE_EQ(rexc::format_type(assign.target->type), std::string("*i32"));
	REQUIRE_EQ(rexc::format_type(assign.value->type), std::string("i32"));

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*function.body[3]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Unary);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("i32"));
	const auto &deref = static_cast<const rexc::ir::UnaryValue &>(*ret.value);
	REQUIRE_EQ(deref.op, std::string("*"));
}

TEST_CASE(lowering_lowers_index_expression_as_deref_pointer_add)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; return p[1]; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[2]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Unary);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("i32"));
	const auto &deref = static_cast<const rexc::ir::UnaryValue &>(*ret.value);
	REQUIRE_EQ(deref.op, std::string("*"));
	REQUIRE_EQ(deref.operand->kind, rexc::ir::Value::Kind::Binary);
	const auto &add = static_cast<const rexc::ir::BinaryValue &>(*deref.operand);
	REQUIRE_EQ(add.op, std::string("+"));
	REQUIRE_EQ(rexc::format_type(add.type), std::string("*i32"));
	REQUIRE_EQ(rexc::format_type(add.lhs->type), std::string("*i32"));
	REQUIRE_EQ(rexc::format_type(add.rhs->type), std::string("i32"));
}

TEST_CASE(lowering_lowers_string_index_expression_as_byte_load)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let value: str = \"ok\"; let byte: u8 = value[1]; return byte as i32; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &let = static_cast<const rexc::ir::LetStatement &>(*module.functions[0].body[1]);
	REQUIRE_EQ(let.value->kind, rexc::ir::Value::Kind::Unary);
	REQUIRE_EQ(rexc::format_type(let.value->type), std::string("u8"));
	const auto &deref = static_cast<const rexc::ir::UnaryValue &>(*let.value);
	REQUIRE_EQ(deref.op, std::string("*"));
	REQUIRE_EQ(deref.operand->kind, rexc::ir::Value::Kind::Binary);
	const auto &add = static_cast<const rexc::ir::BinaryValue &>(*deref.operand);
	REQUIRE_EQ(add.op, std::string("+"));
	REQUIRE_EQ(rexc::format_type(add.type), std::string("*u8"));
	REQUIRE_EQ(rexc::format_type(add.lhs->type), std::string("str"));
	REQUIRE_EQ(rexc::format_type(add.rhs->type), std::string("i32"));
}

TEST_CASE(lowering_lowers_if_else_statement)
{
	rexc::SourceFile source(
	    "test.rx", "fn main() -> i32 { if 1 < 2 { return 1; } else { return 0; } }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &stmt = *module.functions[0].body[0];
	REQUIRE_EQ(stmt.kind, rexc::ir::Statement::Kind::If);
	const auto &if_stmt = static_cast<const rexc::ir::IfStatement &>(stmt);
	REQUIRE_EQ(if_stmt.condition->type, rexc::PrimitiveType{rexc::PrimitiveKind::Bool});
	REQUIRE_EQ(if_stmt.then_body.size(), std::size_t(1));
	REQUIRE_EQ(if_stmt.else_body.size(), std::size_t(1));
}

TEST_CASE(lowering_lowers_assignment_and_while_statement)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut x: i32 = 0; while x < 3 { x = x + 1; } return x; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &function = module.functions[0];
	REQUIRE_EQ(function.body.size(), std::size_t(3));
	REQUIRE_EQ(function.body[1]->kind, rexc::ir::Statement::Kind::While);

	const auto &while_stmt = static_cast<const rexc::ir::WhileStatement &>(*function.body[1]);
	REQUIRE_EQ(while_stmt.condition->type, rexc::PrimitiveType{rexc::PrimitiveKind::Bool});
	REQUIRE_EQ(while_stmt.body.size(), std::size_t(1));
	REQUIRE_EQ(while_stmt.body[0]->kind, rexc::ir::Statement::Kind::Assign);

	const auto &assign = static_cast<const rexc::ir::AssignStatement &>(*while_stmt.body[0]);
	REQUIRE_EQ(assign.name, std::string("x"));
	REQUIRE_EQ(rexc::format_type(assign.value->type), std::string("i32"));
}

TEST_CASE(lowering_lowers_increment_decrement_as_unary_expr_statements)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut i: i32 = 0; ++i; i++; --i; i--; return i; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions[0].body[1]->kind, rexc::ir::Statement::Kind::Expr);
	const auto &expr = static_cast<const rexc::ir::ExprStatement &>(*module.functions[0].body[1]);
	REQUIRE_EQ(expr.value->kind, rexc::ir::Value::Kind::Unary);
	const auto &unary = static_cast<const rexc::ir::UnaryValue &>(*expr.value);
	REQUIRE_EQ(unary.op, std::string("pre++"));
}

TEST_CASE(lowering_lowers_match_statement)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut value: i32 = 0; match value { 1 => { value = 10; } _ => { value = 20; } } return value; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions[0].body[1]->kind, rexc::ir::Statement::Kind::Match);
	const auto &match = static_cast<const rexc::ir::MatchStatement &>(*module.functions[0].body[1]);
	REQUIRE_EQ(rexc::format_type(match.value->type), std::string("i32"));
	REQUIRE_EQ(match.arms.size(), std::size_t(2));
	REQUIRE_EQ(match.arms[0].patterns.size(), std::size_t(1));
	REQUIRE_EQ(match.arms[0].patterns[0].kind, rexc::ir::MatchPattern::Kind::Integer);
	REQUIRE_EQ(match.arms[1].patterns.size(), std::size_t(1));
	REQUIRE_EQ(match.arms[1].patterns[0].kind, rexc::ir::MatchPattern::Kind::Default);
}

TEST_CASE(lowering_preserves_multiple_match_patterns_per_arm)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut value: i32 = 0; match value { 1 | 2 => { value = 10; } _ => { value = 20; } } return value; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const auto &match = static_cast<const rexc::ir::MatchStatement &>(*module.functions[0].body[1]);
	REQUIRE_EQ(match.arms[0].patterns.size(), std::size_t(2));
	REQUIRE_EQ(match.arms[0].patterns[0].literal, std::string("1"));
	REQUIRE_EQ(match.arms[0].patterns[1].literal, std::string("2"));
}

TEST_CASE(lowering_lowers_break_and_continue_statements)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { while true { continue; break; } return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &while_stmt =
	    static_cast<const rexc::ir::WhileStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(while_stmt.body.size(), std::size_t(2));
	REQUIRE_EQ(while_stmt.body[0]->kind, rexc::ir::Statement::Kind::Continue);
	REQUIRE_EQ(while_stmt.body[1]->kind, rexc::ir::Statement::Kind::Break);
}

TEST_CASE(lowering_lowers_call_statement)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { println(\"hello\"); return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	auto sema = rexc::analyze_module(parsed.module(), diagnostics);
	REQUIRE(sema.ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const auto &stmt = *module.functions[0].body[0];
	REQUIRE_EQ(stmt.kind, rexc::ir::Statement::Kind::Expr);
	const auto &expr_stmt = static_cast<const rexc::ir::ExprStatement &>(stmt);
	REQUIRE_EQ(expr_stmt.value->kind, rexc::ir::Value::Kind::Call);
	const auto &call = static_cast<const rexc::ir::CallValue &>(*expr_stmt.value);
	REQUIRE_EQ(call.callee, std::string("println"));
	REQUIRE_EQ(call.arguments.size(), std::size_t(1));
}

// FE-103 (Phase 2): generic functions monomorphize at call sites with
// per-instantiation mangled symbols.

TEST_CASE(lowering_monomorphizes_generic_identity_per_instantiation)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn id<T>(x: T) -> T { return x; }\n"
	    "fn main() -> i32 {\n"
	    "  let a: i32 = id(7);\n"
	    "  let b: bool = id(true);\n"
	    "  if b { return a; }\n"
	    "  return 0;\n"
	    "}\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	// main + two monomorphs (id__i32 and id__bool); the generic template
	// itself is NOT emitted because it has unresolved type variables.
	REQUIRE_EQ(module.functions.size(), std::size_t(3));
	bool found_i32 = false;
	bool found_bool = false;
	for (const auto &f : module.functions) {
		if (f.name == "id__i32") found_i32 = true;
		if (f.name == "id__bool") found_bool = true;
	}
	REQUIRE(found_i32);
	REQUIRE(found_bool);
}

TEST_CASE(lowering_routes_generic_call_to_mangled_symbol)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn id<T>(x: T) -> T { return x; }\n"
	    "fn main() -> i32 { return id(42); }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	// Find main and inspect its return statement.
	const rexc::ir::Function *main_fn = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "main") main_fn = &f;
	REQUIRE(main_fn != nullptr);
	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*main_fn->body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Call);
	const auto &call = static_cast<const rexc::ir::CallValue &>(*ret.value);
	REQUIRE_EQ(call.callee, std::string("id__i32"));
}

// FE-103 (Phase 2): generic struct monomorphization at type sites.

TEST_CASE(lowering_monomorphizes_generic_struct_per_type_argument)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Box<T> { value: T }\n"
	    "fn make_i32(x: i32) -> Box<i32> { return Box { value: x }; }\n"
	    "fn make_bool(x: bool) -> Box<bool> { return Box { value: x }; }\n"
	    "fn main() -> i32 {\n"
	    "  let a: Box<i32> = make_i32(7);\n"
	    "  let b: Box<bool> = make_bool(true);\n"
	    "  return a.value;\n"
	    "}\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	// The generic Box template is NOT emitted; each instantiation is lowered
	// via its mangled type identity. Function symbols stay as-is (functions
	// here are not generic).
	const rexc::ir::Function *make_i32 = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "make_i32") make_i32 = &f;
	REQUIRE(make_i32 != nullptr);
	// make_i32's return type uses the mangled struct name.
	REQUIRE_EQ(make_i32->return_type.kind, rexc::PrimitiveKind::UserStruct);
	REQUIRE_EQ(make_i32->return_type.name, std::string("Box__i32"));
}

// Turbofish 'Vec::<i32> { ... }' must instantiate the same Vec__i32
// monomorph that `let v: Vec<i32> = Vec { ... }` already produces via
// FE-103.1's expected-type adoption.
TEST_CASE(lowering_monomorphizes_turbofish_struct_literal)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Vec<T> { data: *T, len: i32, capacity: i32 }\n"
	    "fn make() -> Vec<i32> {\n"
	    "    return Vec::<i32> { data: 0 as *i32, len: 0, capacity: 4 };\n"
	    "}\n"
	    "fn main() -> i32 { let v: Vec<i32> = make(); return v.len; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const rexc::ir::Function *make_fn = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "make") make_fn = &f;
	REQUIRE(make_fn != nullptr);
	REQUIRE_EQ(make_fn->return_type.kind, rexc::PrimitiveKind::UserStruct);
	REQUIRE_EQ(make_fn->return_type.name, std::string("Vec__i32"));
}

TEST_CASE(lowering_monomorphizes_pointer_pattern_correctly)
{
	rexc::SourceFile source(
	    "test.rx",
	    "unsafe fn read<T>(p: *T) -> T { return *p; }\n"
	    "fn main() -> i32 {\n"
	    "  let mut x: i32 = 7;\n"
	    "  let p: *i32 = &x;\n"
	    "  unsafe { return read(p); }\n"
	    "}\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	bool found = false;
	for (const auto &f : module.functions)
		if (f.name == "read__i32") found = true;
	REQUIRE(found);
}

// FE-103 (Phase 2, recursive): template Tree<T> with self-referential
// pointer fields parses + checks + lowers without infinite recursion or
// diagnostics, even when the template is never instantiated.
TEST_CASE(lowering_supports_recursive_generic_struct_template)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Tree<T> { value: T, left: *Tree<T>, right: *Tree<T> }\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	// Lowering must not hang or crash even though Tree<T> is recursive.
	auto module = rexc::lower_to_ir(parsed.module());

	// The generic Tree template itself produces no concrete IR symbols. The
	// observable artifact for an un-instantiated template is the absence of
	// monomorphs — main returns an i32 literal.
	const rexc::ir::Function *main_fn = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "main") main_fn = &f;
	REQUIRE(main_fn != nullptr);
	REQUIRE_EQ(main_fn->return_type.kind, rexc::PrimitiveKind::SignedInteger);
}

// FE-103 (Phase 2, recursive): instantiating Tree<i32> through a function
// signature produces the mangled struct identity once and terminates.
TEST_CASE(lowering_instantiates_recursive_generic_struct_once)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Tree<T> { value: T, left: *Tree<T>, right: *Tree<T> }\n"
	    "fn make(p: *Tree<i32>) -> *Tree<i32> { return p; }\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	// `make` returns *Tree<i32>; the pointee carries the mangled name.
	const rexc::ir::Function *make_fn = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "make") make_fn = &f;
	REQUIRE(make_fn != nullptr);
	REQUIRE_EQ(make_fn->return_type.kind, rexc::PrimitiveKind::Pointer);
	REQUIRE(make_fn->return_type.pointee != nullptr);
	REQUIRE_EQ(make_fn->return_type.pointee->kind,
	    rexc::PrimitiveKind::UserStruct);
	REQUIRE_EQ(make_fn->return_type.pointee->name, std::string("Tree__i32"));
}

// FE-107: defer cleanup is materialized as IR statements at scope-exit.
// `defer call();` itself produces no IR statement at its source line —
// instead the cleanup body is duplicated before each early exit and at
// the end of the enclosing block.
//
// This test wires a function with two defers and an early `return` and
// inspects how many ExprStatement+Call("cleanup") pairs land in the
// lowered body. There must be:
//   * at least 1 cleanup before the early return (inside the if-then),
//   * at least 1 cleanup at end-of-function for the normal exit path.
namespace {
std::size_t count_defer_calls(
    const std::vector<std::unique_ptr<rexc::ir::Statement>> &body,
    const std::string &callee)
{
	std::size_t count = 0;
	for (const auto &stmt : body) {
		if (!stmt)
			continue;
		if (stmt->kind == rexc::ir::Statement::Kind::Expr) {
			const auto &expr = static_cast<const rexc::ir::ExprStatement &>(*stmt);
			if (expr.value && expr.value->kind == rexc::ir::Value::Kind::Call) {
				const auto &call =
				    static_cast<const rexc::ir::CallValue &>(*expr.value);
				if (call.callee == callee)
					++count;
			}
		}
		if (stmt->kind == rexc::ir::Statement::Kind::If) {
			const auto &if_stmt = static_cast<const rexc::ir::IfStatement &>(*stmt);
			count += count_defer_calls(if_stmt.then_body, callee);
			count += count_defer_calls(if_stmt.else_body, callee);
		}
		if (stmt->kind == rexc::ir::Statement::Kind::While) {
			const auto &while_stmt =
			    static_cast<const rexc::ir::WhileStatement &>(*stmt);
			count += count_defer_calls(while_stmt.body, callee);
		}
		if (stmt->kind == rexc::ir::Statement::Kind::For) {
			const auto &for_stmt =
			    static_cast<const rexc::ir::ForStatement &>(*stmt);
			count += count_defer_calls(for_stmt.body, callee);
		}
		if (stmt->kind == rexc::ir::Statement::Kind::Match) {
			const auto &match_stmt =
			    static_cast<const rexc::ir::MatchStatement &>(*stmt);
			for (const auto &arm : match_stmt.arms)
				count += count_defer_calls(arm.body, callee);
		}
	}
	return count;
}
} // namespace

TEST_CASE(lowering_emits_defer_cleanup_at_block_end)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn cleanup() -> i32 { return 0; }\n"
	    "fn run() -> i32 {\n"
	    "    defer cleanup();\n"
	    "    return 7;\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const rexc::ir::Function *run = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "run") run = &f;
	REQUIRE(run != nullptr);
	// `defer cleanup();` materializes as an ExprStatement(Call("cleanup"))
	// emitted *before* the return. (End-of-block cleanup is unreachable
	// after the return, so the lowering may emit either one or two
	// instances depending on how it threads the LIFO queue — the
	// invariant is "at least one".)
	REQUIRE(count_defer_calls(run->body, "cleanup") >= 1);
}

TEST_CASE(lowering_emits_defer_cleanup_before_each_early_exit)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn cleanup() -> i32 { return 0; }\n"
	    "fn run(x: i32) -> i32 {\n"
	    "    defer cleanup();\n"
	    "    if x == 1 { return 11; }\n"
	    "    return 22;\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const rexc::ir::Function *run = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "run") run = &f;
	REQUIRE(run != nullptr);
	// One cleanup before the early return inside the `if`, one before
	// the trailing return — both reachable code paths must run cleanup.
	REQUIRE(count_defer_calls(run->body, "cleanup") >= 2);
}

// FE-107 + FE-012: defer must fire on the early-return path injected by
// the `?` operator. The cleanup lands inside the if-then-body that `?`
// expands to, so it only runs on the Err propagation path.
TEST_CASE(lowering_emits_defer_cleanup_for_try_operator_early_return)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn cleanup() -> i32 { return 0; }\n"
	    "fn fallible() -> Result<i32> { return result_i32_ok(7); }\n"
	    "fn caller() -> Result<i32> {\n"
	    "    defer cleanup();\n"
	    "    let v: i32 = fallible()?;\n"
	    "    return result_i32_ok(v);\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	rexc::SemanticOptions options;
	options.stdlib_symbols = rexc::StdlibSymbolPolicy::All;
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics, options).ok());

	rexc::LowerOptions lower_options;
	lower_options.stdlib_symbols = rexc::LowerStdlibSymbolPolicy::All;
	auto module = rexc::lower_to_ir(parsed.module(), lower_options);
	const rexc::ir::Function *caller = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "caller") caller = &f;
	REQUIRE(caller != nullptr);
	// Cleanup must appear: (a) inside the if-then-body emitted by the
	// `?` lowering for the Err early-return, and (b) at least one more
	// site for the trailing `return result_i32_ok(v)` cleanup. Counting
	// total cleanup() call sites must therefore be >= 2.
	REQUIRE(count_defer_calls(caller->body, "cleanup") >= 2);
}

TEST_CASE(lowering_emits_lifo_defer_order)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn first() -> i32 { return 0; }\n"
	    "fn second() -> i32 { return 0; }\n"
	    "fn run() -> i32 {\n"
	    "    defer first();\n"
	    "    defer second();\n"
	    "    return 0;\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const rexc::ir::Function *run = nullptr;
	for (const auto &f : module.functions)
		if (f.name == "run") run = &f;
	REQUIRE(run != nullptr);
	// Walk the IR statements in order; the first cleanup callee
	// emitted must be `second` (last-registered), then `first`.
	std::vector<std::string> order;
	for (const auto &stmt : run->body) {
		if (!stmt || stmt->kind != rexc::ir::Statement::Kind::Expr)
			continue;
		const auto &expr = static_cast<const rexc::ir::ExprStatement &>(*stmt);
		if (!expr.value || expr.value->kind != rexc::ir::Value::Kind::Call)
			continue;
		const auto &call = static_cast<const rexc::ir::CallValue &>(*expr.value);
		if (call.callee == "first" || call.callee == "second")
			order.push_back(call.callee);
	}
	REQUIRE(order.size() >= 2u);
	REQUIRE_EQ(order[0], std::string("second"));
	REQUIRE_EQ(order[1], std::string("first"));
}
