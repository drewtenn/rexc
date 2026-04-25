#include "rexc/codegen_x86.hpp"
#include "rexc/types.hpp"

#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_map>

namespace rexc {
namespace {

struct Frame {
	std::unordered_map<std::string, int> slots;
	int local_bytes = 0;
};

int count_locals(const ir::Function &function)
{
	int count = 0;
	for (const auto &statement : function.body) {
		if (statement->kind == ir::Statement::Kind::Let)
			++count;
	}
	return count;
}

std::string canonical_decimal_literal(const std::string &literal)
{
	std::size_t first_non_zero = literal.find_first_not_of('0');
	if (first_non_zero == std::string::npos)
		return "0";
	return literal.substr(first_non_zero);
}

class Emitter {
public:
	std::string emit(const ir::Module &module)
	{
		out_ << ".text\n";
		for (const auto &function : module.functions) {
			if (!function.is_extern)
				emit_function(function);
		}
		return out_.str();
	}

private:
	void emit_function(const ir::Function &function)
	{
		guard_supported_function(function);

		Frame frame = build_frame(function);
		std::string done_label = ".L_return_" + function.name;

		out_ << ".globl " << function.name << '\n';
		out_ << function.name << ":\n";
		out_ << "\tpushl %ebp\n";
		out_ << "\tmovl %esp, %ebp\n";
		if (frame.local_bytes > 0)
			out_ << "\tsubl $" << frame.local_bytes << ", %esp\n";

		for (const auto &statement : function.body)
			emit_statement(*statement, frame, done_label);

		out_ << done_label << ":\n";
		out_ << "\tleave\n";
		out_ << "\tret\n\n";
	}

	Frame build_frame(const ir::Function &function)
	{
		Frame frame;
		int offset = 8;
		for (const auto &parameter : function.parameters) {
			frame.slots[parameter.name] = offset;
			offset += 4;
		}

		int local_index = 0;
		for (const auto &statement : function.body) {
			if (statement->kind != ir::Statement::Kind::Let)
				continue;
			const auto &let = static_cast<const ir::LetStatement &>(*statement);
			++local_index;
			frame.slots[let.name] = -4 * local_index;
		}
		frame.local_bytes = count_locals(function) * 4;
		return frame;
	}

	void guard_supported_function(const ir::Function &function)
	{
		guard_supported_type(function.return_type);
		for (const auto &parameter : function.parameters)
			guard_supported_type(parameter.type);
	}

	void emit_statement(const ir::Statement &statement, const Frame &frame,
	                    const std::string &done_label)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			const auto &let = static_cast<const ir::LetStatement &>(statement);
			emit_value(*let.value, frame);
			out_ << "\tmovl %eax, " << frame.slots.at(let.name) << "(%ebp)\n";
			return;
		}

		const auto &ret = static_cast<const ir::ReturnStatement &>(statement);
		emit_value(*ret.value, frame);
		out_ << "\tjmp " << done_label << '\n';
	}

	void emit_value(const ir::Value &value, const Frame &frame)
	{
		guard_supported_value(value);

		switch (value.kind) {
		case ir::Value::Kind::Integer: {
			const auto &integer = static_cast<const ir::IntegerValue &>(value);
			out_ << "\tmovl $";
			if (integer.is_negative)
				out_ << '-';
			out_ << canonical_decimal_literal(integer.literal) << ", %eax\n";
			return;
		}
		case ir::Value::Kind::Bool: {
			const auto &boolean = static_cast<const ir::BoolValue &>(value);
			out_ << "\tmovl $" << (boolean.value ? 1 : 0) << ", %eax\n";
			return;
		}
		case ir::Value::Kind::Char: {
			const auto &character = static_cast<const ir::CharValue &>(value);
			out_ << "\tmovl $" << static_cast<unsigned int>(character.value) << ", %eax\n";
			return;
		}
		case ir::Value::Kind::String:
			throw std::runtime_error("string code generation is not implemented");
		case ir::Value::Kind::Unary:
			emit_unary(static_cast<const ir::UnaryValue &>(value), frame);
			return;
		case ir::Value::Kind::Local: {
			const auto &local = static_cast<const ir::LocalValue &>(value);
			out_ << "\tmovl " << frame.slots.at(local.name) << "(%ebp), %eax\n";
			return;
		}
		case ir::Value::Kind::Binary:
			emit_binary(static_cast<const ir::BinaryValue &>(value), frame);
			return;
		case ir::Value::Kind::Call:
			emit_call(static_cast<const ir::CallValue &>(value), frame);
			return;
		}
	}

	void guard_supported_value(const ir::Value &value)
	{
		guard_supported_type(value.type);
	}

	void guard_supported_type(ir::Type type)
	{
		if (is_integer(type) && type.bits == 64)
			throw std::runtime_error("64-bit integer code generation is not implemented");
		if (type.kind == PrimitiveKind::Str)
			throw std::runtime_error("string code generation is not implemented");
	}

	void emit_unary(const ir::UnaryValue &unary, const Frame &frame)
	{
		emit_value(*unary.operand, frame);
		if (unary.op == "-")
			out_ << "\tnegl %eax\n";
	}

	void emit_binary(const ir::BinaryValue &binary, const Frame &frame)
	{
		if (binary.op == "/" && is_unsigned_integer(binary.type))
			throw std::runtime_error("unsigned division code generation is not implemented");

		emit_value(*binary.lhs, frame);
		out_ << "\tpushl %eax\n";
		emit_value(*binary.rhs, frame);
		out_ << "\tmovl %eax, %ecx\n";
		out_ << "\tpopl %eax\n";

		if (binary.op == "+")
			out_ << "\taddl %ecx, %eax\n";
		else if (binary.op == "-")
			out_ << "\tsubl %ecx, %eax\n";
		else if (binary.op == "*")
			out_ << "\timull %ecx, %eax\n";
		else if (binary.op == "/") {
			out_ << "\tcltd\n";
			out_ << "\tidivl %ecx\n";
		}
	}

	void emit_call(const ir::CallValue &call, const Frame &frame)
	{
		for (auto it = call.arguments.rbegin(); it != call.arguments.rend(); ++it) {
			emit_value(**it, frame);
			out_ << "\tpushl %eax\n";
		}

		out_ << "\tcall " << call.callee << '\n';
		if (!call.arguments.empty())
			out_ << "\taddl $" << call.arguments.size() * 4 << ", %esp\n";
	}

	std::ostringstream out_;
};

} // namespace

std::string emit_x86_assembly(const ir::Module &module)
{
	Emitter emitter;
	return emitter.emit(module);
}

} // namespace rexc
