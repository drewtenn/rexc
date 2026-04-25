#include "rexc/codegen_x86.hpp"
#include "rexc/types.hpp"

#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

std::string escape_asciz_payload(const std::string &payload)
{
	std::ostringstream escaped;
	for (unsigned char ch : payload) {
		switch (ch) {
		case '\n':
			escaped << "\\n";
			break;
		case '\r':
			escaped << "\\r";
			break;
		case '\t':
			escaped << "\\t";
			break;
		case '"':
			escaped << "\\\"";
			break;
		case '\\':
			escaped << "\\\\";
			break;
		default:
			if (ch >= 0x20 && ch <= 0x7e) {
				escaped << static_cast<char>(ch);
			} else {
				escaped << '\\'
				        << static_cast<char>('0' + ((ch >> 6) & 7))
				        << static_cast<char>('0' + ((ch >> 3) & 7))
				        << static_cast<char>('0' + (ch & 7));
			}
			break;
		}
	}
	return escaped.str();
}

std::string unsupported_codegen_message(ir::Type type)
{
	if (is_integer(type) && type.bits == 64)
		return "64-bit integer code generation is not implemented for i386";
	return format_type(type) + " code generation is not implemented for i386";
}

class Emitter {
public:
	explicit Emitter(Diagnostics &diagnostics) : diagnostics_(diagnostics) {}

	CodegenResult emit(const ir::Module &module)
	{
		std::size_t starting_diagnostics = diagnostics_.items().size();

		validate_module(module);
		if (diagnostics_.items().size() != starting_diagnostics)
			return CodegenResult(false, "");

		collect_string_labels(module);
		emit_string_section(module);

		out_ << ".text\n";
		for (const auto &function : module.functions) {
			if (function.is_extern)
				continue;
			emit_function(function);
		}

		return CodegenResult(true, out_.str());
	}

private:
	void emit_function(const ir::Function &function)
	{
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

	bool validate_module(const ir::Module &module)
	{
		bool ok = true;
		for (const auto &function : module.functions)
			ok = validate_function(function) && ok;
		return ok;
	}

	bool validate_function(const ir::Function &function)
	{
		bool ok = validate_type(function.return_type);
		for (const auto &parameter : function.parameters)
			ok = validate_type(parameter.type) && ok;
		for (const auto &statement : function.body)
			ok = validate_statement(*statement) && ok;
		return ok;
	}

	bool validate_statement(const ir::Statement &statement)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			const auto &let = static_cast<const ir::LetStatement &>(statement);
			return validate_value(*let.value);
		}

		const auto &ret = static_cast<const ir::ReturnStatement &>(statement);
		return validate_value(*ret.value);
	}

	bool validate_value(const ir::Value &value)
	{
		bool ok = validate_type(value.type);

		switch (value.kind) {
		case ir::Value::Kind::Integer:
		case ir::Value::Kind::Bool:
		case ir::Value::Kind::Char:
		case ir::Value::Kind::String:
		case ir::Value::Kind::Local:
			return ok;
		case ir::Value::Kind::Unary: {
			const auto &unary = static_cast<const ir::UnaryValue &>(value);
			return validate_value(*unary.operand) && ok;
		}
		case ir::Value::Kind::Binary: {
			const auto &binary = static_cast<const ir::BinaryValue &>(value);
			ok = validate_value(*binary.lhs) && ok;
			ok = validate_value(*binary.rhs) && ok;
			return ok;
		}
		case ir::Value::Kind::Call: {
			const auto &call = static_cast<const ir::CallValue &>(value);
			for (const auto &argument : call.arguments)
				ok = validate_value(*argument) && ok;
			return ok;
		}
		}

		throw std::runtime_error("unexpected IR value kind during validation");
	}

	bool validate_type(ir::Type type)
	{
		if (is_i386_codegen_supported(type))
			return true;
		std::string message = unsupported_codegen_message(type);
		if (unsupported_diagnostics_.insert(message).second)
			diagnostics_.error({}, std::move(message));
		return false;
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
		case ir::Value::Kind::String: {
			const auto &string = static_cast<const ir::StringValue &>(value);
			out_ << "\tmovl $" << string_labels_.at(&string) << ", %eax\n";
			return;
		}
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

	void emit_unary(const ir::UnaryValue &unary, const Frame &frame)
	{
		emit_value(*unary.operand, frame);
		if (unary.op == "-")
			out_ << "\tnegl %eax\n";
	}

	void emit_binary(const ir::BinaryValue &binary, const Frame &frame)
	{
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
			if (is_unsigned_integer(binary.type)) {
				out_ << "\txorl %edx, %edx\n";
				out_ << "\tdivl %ecx\n";
			} else {
				out_ << "\tcltd\n";
				out_ << "\tidivl %ecx\n";
			}
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

	void collect_string_labels(const ir::Module &module)
	{
		for (const auto &function : module.functions) {
			for (const auto &statement : function.body)
				collect_string_labels(*statement);
		}
	}

	void collect_string_labels(const ir::Statement &statement)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			const auto &let = static_cast<const ir::LetStatement &>(statement);
			collect_string_labels(*let.value);
			return;
		}

		const auto &ret = static_cast<const ir::ReturnStatement &>(statement);
		collect_string_labels(*ret.value);
	}

	void collect_string_labels(const ir::Value &value)
	{
		switch (value.kind) {
		case ir::Value::Kind::Integer:
		case ir::Value::Kind::Bool:
		case ir::Value::Kind::Char:
		case ir::Value::Kind::Local:
			return;
		case ir::Value::Kind::String: {
			const auto &string = static_cast<const ir::StringValue &>(value);
			string_labels_[&string] = ".Lstr" + std::to_string(string_labels_.size());
			return;
		}
		case ir::Value::Kind::Unary: {
			const auto &unary = static_cast<const ir::UnaryValue &>(value);
			collect_string_labels(*unary.operand);
			return;
		}
		case ir::Value::Kind::Binary: {
			const auto &binary = static_cast<const ir::BinaryValue &>(value);
			collect_string_labels(*binary.lhs);
			collect_string_labels(*binary.rhs);
			return;
		}
		case ir::Value::Kind::Call: {
			const auto &call = static_cast<const ir::CallValue &>(value);
			for (const auto &argument : call.arguments)
				collect_string_labels(*argument);
			return;
		}
		}
	}

	void emit_string_section(const ir::Module &module)
	{
		if (string_labels_.empty())
			return;

		out_ << ".section .rodata\n";
		for (const auto &function : module.functions) {
			for (const auto &statement : function.body)
				emit_string_literals(*statement);
		}
	}

	void emit_string_literals(const ir::Statement &statement)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			const auto &let = static_cast<const ir::LetStatement &>(statement);
			emit_string_literals(*let.value);
			return;
		}

		const auto &ret = static_cast<const ir::ReturnStatement &>(statement);
		emit_string_literals(*ret.value);
	}

	void emit_string_literals(const ir::Value &value)
	{
		switch (value.kind) {
		case ir::Value::Kind::Integer:
		case ir::Value::Kind::Bool:
		case ir::Value::Kind::Char:
		case ir::Value::Kind::Local:
			return;
		case ir::Value::Kind::String: {
			const auto &string = static_cast<const ir::StringValue &>(value);
			out_ << string_labels_.at(&string) << ":\n";
			out_ << "\t.asciz \"" << escape_asciz_payload(string.value) << "\"\n";
			return;
		}
		case ir::Value::Kind::Unary: {
			const auto &unary = static_cast<const ir::UnaryValue &>(value);
			emit_string_literals(*unary.operand);
			return;
		}
		case ir::Value::Kind::Binary: {
			const auto &binary = static_cast<const ir::BinaryValue &>(value);
			emit_string_literals(*binary.lhs);
			emit_string_literals(*binary.rhs);
			return;
		}
		case ir::Value::Kind::Call: {
			const auto &call = static_cast<const ir::CallValue &>(value);
			for (const auto &argument : call.arguments)
				emit_string_literals(*argument);
			return;
		}
		}
	}

	Diagnostics &diagnostics_;
	std::ostringstream out_;
	std::unordered_map<const ir::StringValue *, std::string> string_labels_;
	std::unordered_set<std::string> unsupported_diagnostics_;
};

} // namespace

CodegenResult::CodegenResult(bool ok, std::string assembly)
	: ok_(ok), assembly_(std::move(assembly))
{
}

bool CodegenResult::ok() const
{
	return ok_;
}

const std::string &CodegenResult::assembly() const
{
	return assembly_;
}

CodegenResult emit_x86_assembly(const ir::Module &module, Diagnostics &diagnostics)
{
	Emitter emitter(diagnostics);
	return emitter.emit(module);
}

} // namespace rexc
