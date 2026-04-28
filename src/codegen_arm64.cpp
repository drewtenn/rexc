// Darwin ARM64 assembly backend for Rexy's typed IR.
#include "rexc/codegen_arm64.hpp"
#include "rexc/types.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rexc {
namespace {

using SlotMap = std::unordered_map<std::string, int>;

struct Frame {
	SlotMap parameter_slots;
	std::unordered_map<const ir::LetStatement *, int> let_slots;
	int local_bytes = 0;
};

bool is_comparison_operator(const std::string &op)
{
	return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
	       op == ">=";
}

bool is_logical_operator(const std::string &op)
{
	return op == "&&" || op == "||";
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

std::string darwin_symbol(const std::string &name)
{
	return "_" + name;
}

int count_local_statement(const ir::Statement &statement);

int count_locals(const std::vector<std::unique_ptr<ir::Statement>> &statements)
{
	int count = 0;
	for (const auto &statement : statements)
		count += count_local_statement(*statement);
	return count;
}

int count_local_statement(const ir::Statement &statement)
{
	if (statement.kind == ir::Statement::Kind::Let)
		return 1;
	if (statement.kind == ir::Statement::Kind::While) {
		const auto &while_statement = static_cast<const ir::WhileStatement &>(statement);
		return count_locals(while_statement.body);
	}
	if (statement.kind == ir::Statement::Kind::For) {
		const auto &for_statement = static_cast<const ir::ForStatement &>(statement);
		return count_local_statement(*for_statement.initializer) +
		       count_locals(for_statement.body) +
		       count_local_statement(*for_statement.increment);
	}
	if (statement.kind == ir::Statement::Kind::If) {
		const auto &if_statement = static_cast<const ir::IfStatement &>(statement);
		return count_locals(if_statement.then_body) + count_locals(if_statement.else_body);
	}
	return 0;
}

int align16(int bytes)
{
	return bytes == 0 ? 0 : ((bytes + 15) / 16) * 16;
}

class Arm64Emitter {
public:
	explicit Arm64Emitter(Diagnostics &diagnostics)
		: diagnostics_(diagnostics)
	{
	}

	CodegenResult emit(const ir::Module &module)
	{
		std::size_t starting_diagnostics = diagnostics_.items().size();
		validate_module(module);
		if (diagnostics_.items().size() != starting_diagnostics)
			return CodegenResult(false, "");

		collect_static_buffer_names(module);
		collect_string_labels(module);
		emit_string_section(module);
		emit_static_scalar_section(module);
		emit_static_buffer_section(module);

		out_ << ".text\n";
		for (const auto &function : module.functions) {
			if (function.is_extern)
				continue;
			emit_function(function);
		}
		return CodegenResult(true, out_.str());
	}

private:
	void collect_static_buffer_names(const ir::Module &module)
	{
		static_buffer_names_.clear();
		for (const auto &buffer : module.static_buffers)
			static_buffer_names_.insert(buffer.name);
	}

	void validate_module(const ir::Module &module)
	{
		for (const auto &buffer : module.static_buffers)
			validate_type(buffer.element_type);
		for (const auto &scalar : module.static_scalars)
			validate_type(scalar.type);
		for (const auto &function : module.functions) {
			validate_type(function.return_type);
			for (const auto &parameter : function.parameters)
				validate_type(parameter.type);
		}
	}

	void validate_type(ir::Type type)
	{
		if (!is_valid_primitive_type(type))
			diagnostics_.error({}, "invalid type during arm64 code generation");
	}

	void emit_function(const ir::Function &function)
	{
		Frame frame = build_frame(function);
		std::string symbol = darwin_symbol(function.name);
		std::string done_label = "L_return_" + function.name;

		out_ << ".globl " << symbol << '\n';
		out_ << ".p2align 2\n";
		out_ << symbol << ":\n";
		out_ << "\tstp x29, x30, [sp, #-16]!\n";
		out_ << "\tmov x29, sp\n";
		if (frame.local_bytes > 0)
			out_ << "\tsub sp, sp, #" << frame.local_bytes << "\n";
		emit_parameter_spills(function, frame);

		SlotMap slots = frame.parameter_slots;
		for (const auto &statement : function.body)
			emit_statement(*statement, frame, done_label, slots);

		out_ << done_label << ":\n";
		out_ << "\tmov sp, x29\n";
		out_ << "\tldp x29, x30, [sp], #16\n";
		out_ << "\tret\n\n";
	}

	Frame build_frame(const ir::Function &function)
	{
		Frame frame;
		int slot_index = 0;
		for (std::size_t i = 0; i < function.parameters.size(); ++i) {
			const auto &parameter = function.parameters[i];
			if (i < argument_registers().size()) {
				++slot_index;
				frame.parameter_slots[parameter.name] = -8 * slot_index;
			} else {
				frame.parameter_slots[parameter.name] =
					16 + static_cast<int>(i - argument_registers().size()) * 8;
			}
		}

		assign_local_slots(function.body, frame, slot_index);
		frame.local_bytes = align16(slot_index * 8);
		return frame;
	}

	void assign_local_slot(const ir::Statement &statement, Frame &frame, int &slot_index)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			const auto &let = static_cast<const ir::LetStatement &>(statement);
			++slot_index;
			frame.let_slots[&let] = -8 * slot_index;
		} else if (statement.kind == ir::Statement::Kind::While) {
			const auto &while_statement = static_cast<const ir::WhileStatement &>(statement);
			assign_local_slots(while_statement.body, frame, slot_index);
		} else if (statement.kind == ir::Statement::Kind::For) {
			const auto &for_statement = static_cast<const ir::ForStatement &>(statement);
			assign_local_slot(*for_statement.initializer, frame, slot_index);
			assign_local_slots(for_statement.body, frame, slot_index);
			assign_local_slot(*for_statement.increment, frame, slot_index);
		} else if (statement.kind == ir::Statement::Kind::If) {
			const auto &if_statement = static_cast<const ir::IfStatement &>(statement);
			assign_local_slots(if_statement.then_body, frame, slot_index);
			assign_local_slots(if_statement.else_body, frame, slot_index);
		}
	}

	void assign_local_slots(const std::vector<std::unique_ptr<ir::Statement>> &statements,
	                        Frame &frame, int &slot_index)
	{
		for (const auto &statement : statements)
			assign_local_slot(*statement, frame, slot_index);
	}

	void emit_parameter_spills(const ir::Function &function, const Frame &frame)
	{
		std::size_t register_count =
			std::min(function.parameters.size(), argument_registers().size());
		for (std::size_t i = 0; i < register_count; ++i)
			out_ << "\tstr " << argument_registers()[i] << ", [x29, #"
			     << frame.parameter_slots.at(function.parameters[i].name) << "]\n";
	}

	void emit_statement(const ir::Statement &statement, const Frame &frame,
	                    const std::string &done_label, SlotMap &slots)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			const auto &let = static_cast<const ir::LetStatement &>(statement);
			emit_value(*let.value, frame, slots);
			int slot = frame.let_slots.at(&let);
			out_ << "\tstr x0, [x29, #" << slot << "]\n";
			slots[let.name] = slot;
			return;
		}

		if (statement.kind == ir::Statement::Kind::Assign) {
			const auto &assign = static_cast<const ir::AssignStatement &>(statement);
			emit_value(*assign.value, frame, slots);
			if (slots.find(assign.name) == slots.end()) {
				emit_static_scalar_store(assign.name);
				return;
			}
			out_ << "\tstr x0, [x29, #" << slots.at(assign.name) << "]\n";
			return;
		}

		if (statement.kind == ir::Statement::Kind::IndirectAssign) {
			emit_indirect_assign(static_cast<const ir::IndirectAssignStatement &>(statement),
			                     frame, slots);
			return;
		}

		if (statement.kind == ir::Statement::Kind::Expr) {
			const auto &expr = static_cast<const ir::ExprStatement &>(statement);
			emit_value(*expr.value, frame, slots);
			return;
		}

		if (statement.kind == ir::Statement::Kind::If) {
			emit_if_statement(static_cast<const ir::IfStatement &>(statement), frame,
			                  done_label, slots);
			return;
		}

		if (statement.kind == ir::Statement::Kind::While) {
			emit_while_statement(static_cast<const ir::WhileStatement &>(statement), frame,
			                     done_label, slots);
			return;
		}

		if (statement.kind == ir::Statement::Kind::For) {
			emit_for_statement(static_cast<const ir::ForStatement &>(statement), frame,
			                   done_label, slots);
			return;
		}

		if (statement.kind == ir::Statement::Kind::Break) {
			out_ << "\tb " << loop_end_labels_.back() << "\n";
			return;
		}

		if (statement.kind == ir::Statement::Kind::Continue) {
			out_ << "\tb " << loop_start_labels_.back() << "\n";
			return;
		}

		const auto &ret = static_cast<const ir::ReturnStatement &>(statement);
		emit_value(*ret.value, frame, slots);
		out_ << "\tb " << done_label << "\n";
	}

	void emit_if_statement(const ir::IfStatement &if_statement, const Frame &frame,
	                       const std::string &done_label, const SlotMap &slots)
	{
		std::string else_label = make_label("L_else_");
		std::string end_label = make_label("L_end_if_");

		emit_value(*if_statement.condition, frame, slots);
		out_ << "\tcbz w0, " << else_label << "\n";

		SlotMap then_slots = slots;
		for (const auto &statement : if_statement.then_body)
			emit_statement(*statement, frame, done_label, then_slots);
		out_ << "\tb " << end_label << "\n";

		out_ << else_label << ":\n";
		SlotMap else_slots = slots;
		for (const auto &statement : if_statement.else_body)
			emit_statement(*statement, frame, done_label, else_slots);
		out_ << end_label << ":\n";
	}

	void emit_while_statement(const ir::WhileStatement &while_statement, const Frame &frame,
	                          const std::string &done_label, const SlotMap &slots)
	{
		std::string start_label = make_label("L_while_start_");
		std::string end_label = make_label("L_while_end_");

		out_ << start_label << ":\n";
		emit_value(*while_statement.condition, frame, slots);
		out_ << "\tcbz w0, " << end_label << "\n";

		loop_start_labels_.push_back(start_label);
		loop_end_labels_.push_back(end_label);
		SlotMap body_slots = slots;
		for (const auto &statement : while_statement.body)
			emit_statement(*statement, frame, done_label, body_slots);
		loop_end_labels_.pop_back();
		loop_start_labels_.pop_back();
		out_ << "\tb " << start_label << "\n";
		out_ << end_label << ":\n";
	}

	void emit_for_statement(const ir::ForStatement &for_statement, const Frame &frame,
	                        const std::string &done_label, const SlotMap &slots)
	{
		std::string condition_label = make_label("L_for_condition_");
		std::string increment_label = make_label("L_for_increment_");
		std::string end_label = make_label("L_for_end_");

		SlotMap loop_slots = slots;
		emit_statement(*for_statement.initializer, frame, done_label, loop_slots);

		out_ << condition_label << ":\n";
		emit_value(*for_statement.condition, frame, loop_slots);
		out_ << "\tcbz w0, " << end_label << "\n";

		loop_start_labels_.push_back(increment_label);
		loop_end_labels_.push_back(end_label);
		SlotMap body_slots = loop_slots;
		for (const auto &statement : for_statement.body)
			emit_statement(*statement, frame, done_label, body_slots);
		loop_end_labels_.pop_back();
		loop_start_labels_.pop_back();

		out_ << increment_label << ":\n";
		emit_statement(*for_statement.increment, frame, done_label, loop_slots);
		out_ << "\tb " << condition_label << "\n";
		out_ << end_label << ":\n";
	}

	void emit_value(const ir::Value &value, const Frame &frame, const SlotMap &slots)
	{
		switch (value.kind) {
		case ir::Value::Kind::Integer:
			emit_integer(static_cast<const ir::IntegerValue &>(value));
			return;
		case ir::Value::Kind::Bool: {
			const auto &boolean = static_cast<const ir::BoolValue &>(value);
			out_ << "\tmov w0, #" << (boolean.value ? 1 : 0) << "\n";
			return;
		}
		case ir::Value::Kind::Char: {
			const auto &character = static_cast<const ir::CharValue &>(value);
			out_ << "\tmov w0, #" << static_cast<unsigned int>(character.value) << "\n";
			return;
		}
		case ir::Value::Kind::String: {
			const auto &string = static_cast<const ir::StringValue &>(value);
			out_ << "\tadrp x0, " << string_labels_.at(&string) << "@PAGE\n";
			out_ << "\tadd x0, x0, " << string_labels_.at(&string) << "@PAGEOFF\n";
			return;
		}
		case ir::Value::Kind::Global: {
			const auto &global = static_cast<const ir::GlobalValue &>(value);
			if (static_buffer_names_.find(global.name) == static_buffer_names_.end()) {
				emit_static_scalar_load(global.name);
				return;
			}
			out_ << "\tadrp x0, " << static_buffer_label(global.name) << "@PAGE\n";
			out_ << "\tadd x0, x0, " << static_buffer_label(global.name) << "@PAGEOFF\n";
			return;
		}
		case ir::Value::Kind::Local: {
			const auto &local = static_cast<const ir::LocalValue &>(value);
			out_ << "\tldr x0, [x29, #" << slots.at(local.name) << "]\n";
			return;
		}
		case ir::Value::Kind::Unary:
			emit_unary(static_cast<const ir::UnaryValue &>(value), frame, slots);
			return;
		case ir::Value::Kind::Binary:
			emit_binary(static_cast<const ir::BinaryValue &>(value), frame, slots);
			return;
		case ir::Value::Kind::Cast:
			emit_cast(static_cast<const ir::CastValue &>(value), frame, slots);
			return;
		case ir::Value::Kind::Call:
			emit_call(static_cast<const ir::CallValue &>(value), frame, slots);
			return;
		}
	}

	void emit_integer(const ir::IntegerValue &integer)
	{
		std::uint64_t value = std::stoull(canonical_decimal_literal(integer.literal));
		if (integer.is_negative)
			value = 0 - value;
		emit_integer_immediate(value, memory_bits(integer.type));
	}

	void emit_integer_immediate(std::uint64_t value, int bits)
	{
		if (bits <= 32 && value <= 65535) {
			out_ << "\tmov w0, #" << value << "\n";
			return;
		}

		std::string reg = bits <= 32 ? "w0" : "x0";
		int chunk_count = bits <= 32 ? 2 : 4;
		bool emitted = false;
		for (int chunk = 0; chunk < chunk_count; ++chunk) {
			std::uint64_t part = (value >> (chunk * 16)) & 0xffff;
			if (part == 0 && emitted)
				continue;
			if (!emitted) {
				out_ << "\tmovz " << reg << ", #" << part;
				emitted = true;
			} else {
				out_ << "\tmovk " << reg << ", #" << part;
			}
			if (chunk > 0)
				out_ << ", lsl #" << (chunk * 16);
			out_ << "\n";
		}
		if (!emitted)
			out_ << "\tmov " << reg << ", #0\n";
	}

	void emit_cast(const ir::CastValue &cast, const Frame &frame, const SlotMap &slots)
	{
		emit_value(*cast.value, frame, slots);
		if (!is_integer(cast.type))
			return;
		if (cast.type.bits == 8)
			out_ << "\t" << (is_signed_integer(cast.type) ? "sxtb" : "uxtb")
			     << " w0, w0\n";
		else if (cast.type.bits == 16)
			out_ << "\t" << (is_signed_integer(cast.type) ? "sxth" : "uxth")
			     << " w0, w0\n";
		else if (cast.type.bits == 32 && is_signed_integer(cast.type))
			out_ << "\tsxtw x0, w0\n";
	}

	void emit_unary(const ir::UnaryValue &unary, const Frame &frame, const SlotMap &slots)
	{
		if (unary.op == "&") {
			emit_address_of(*unary.operand, slots);
			return;
		}
		if (unary.op == "pre++" || unary.op == "post++" ||
		    unary.op == "pre--" || unary.op == "post--") {
			emit_increment(unary, slots);
			return;
		}

		emit_value(*unary.operand, frame, slots);
		if (unary.op == "-")
			out_ << "\tneg x0, x0\n";
		else if (unary.op == "!") {
			out_ << "\tcmp w0, #0\n";
			out_ << "\tcset w0, eq\n";
		} else if (unary.op == "*") {
			emit_indirect_load(unary.type);
		}
	}

	void emit_increment(const ir::UnaryValue &unary, const SlotMap &slots)
	{
		bool increment = unary.op == "pre++" || unary.op == "post++";
		bool postfix = unary.op == "post++" || unary.op == "post--";
		const auto &operand = *unary.operand;

		if (operand.kind == ir::Value::Kind::Local) {
			const auto &local = static_cast<const ir::LocalValue &>(operand);
			int slot = slots.at(local.name);
			out_ << "\tldr x0, [x29, #" << slot << "]\n";
			if (postfix)
				emit_push_accumulator();
			out_ << "\t" << (increment ? "add" : "sub") << " x0, x0, #1\n";
			out_ << "\tstr x0, [x29, #" << slot << "]\n";
			if (postfix)
				emit_pop_to("x0");
			return;
		}

		const auto &global = static_cast<const ir::GlobalValue &>(operand);
		emit_static_scalar_load(global.name);
		if (postfix)
			emit_push_accumulator();
		out_ << "\t" << (increment ? "add" : "sub") << " x0, x0, #1\n";
		emit_static_scalar_store(global.name);
		if (postfix)
			emit_pop_to("x0");
	}

	void emit_address_of(const ir::Value &operand, const SlotMap &slots)
	{
		const auto &local = static_cast<const ir::LocalValue &>(operand);
		int slot = slots.at(local.name);
		if (slot < 0)
			out_ << "\tsub x0, x29, #" << -slot << "\n";
		else
			out_ << "\tadd x0, x29, #" << slot << "\n";
	}

	void emit_indirect_assign(const ir::IndirectAssignStatement &assign,
	                          const Frame &frame, const SlotMap &slots)
	{
		emit_value(*assign.value, frame, slots);
		emit_push_accumulator();
		emit_value(*assign.target, frame, slots);
		emit_pop_to("x1");
		emit_indirect_store(assign.value->type);
	}

	void emit_indirect_load(ir::Type type)
	{
		int bits = memory_bits(type);
		if (bits == 8) {
			out_ << "\t" << (is_signed_integer(type) ? "ldrsb x0" : "ldrb w0")
			     << ", [x0]\n";
		} else if (bits == 16) {
			out_ << "\t" << (is_signed_integer(type) ? "ldrsh x0" : "ldrh w0")
			     << ", [x0]\n";
		} else if (bits == 32) {
			out_ << "\t" << (is_signed_integer(type) ? "ldrsw x0" : "ldr w0")
			     << ", [x0]\n";
		} else {
			out_ << "\tldr x0, [x0]\n";
		}
	}

	void emit_indirect_store(ir::Type type)
	{
		int bits = memory_bits(type);
		if (bits == 8)
			out_ << "\tstrb w1, [x0]\n";
		else if (bits == 16)
			out_ << "\tstrh w1, [x0]\n";
		else if (bits == 32)
			out_ << "\tstr w1, [x0]\n";
		else
			out_ << "\tstr x1, [x0]\n";
	}

	void emit_binary(const ir::BinaryValue &binary, const Frame &frame, const SlotMap &slots)
	{
		if (is_logical_operator(binary.op)) {
			emit_logical_binary(binary, frame, slots);
			return;
		}

		emit_value(*binary.lhs, frame, slots);
		emit_push_accumulator();
		emit_value(*binary.rhs, frame, slots);
		if (is_pointer(binary.type) && (binary.op == "+" || binary.op == "-"))
			scale_pointer_offset(binary);
		emit_pop_to("x1");

		if (is_comparison_operator(binary.op)) {
			out_ << "\tcmp x1, x0\n";
			out_ << "\tcset w0, " << condition_code(binary) << "\n";
		} else if (binary.op == "+") {
			out_ << "\tadd x0, x1, x0\n";
		} else if (binary.op == "-") {
			out_ << "\tsub x0, x1, x0\n";
		} else if (binary.op == "*") {
			out_ << "\tmul x0, x1, x0\n";
		} else if (binary.op == "/") {
			out_ << "\t" << (is_unsigned_integer(binary.type) ? "udiv" : "sdiv")
			     << " x0, x1, x0\n";
		} else if (binary.op == "%") {
			out_ << "\t" << (is_unsigned_integer(binary.type) ? "udiv" : "sdiv")
			     << " x9, x1, x0\n";
			out_ << "\tmsub x0, x9, x0, x1\n";
		}
	}

	void scale_pointer_offset(const ir::BinaryValue &binary)
	{
		int scale = pointee_size_bytes(binary.type);
		if (scale == 1)
			return;
		out_ << "\tmov x9, #" << scale << "\n";
		out_ << "\tmul x0, x0, x9\n";
	}

	void emit_logical_binary(const ir::BinaryValue &binary, const Frame &frame,
	                         const SlotMap &slots)
	{
		if (binary.op == "&&") {
			std::string false_label = make_label("L_logic_false_");
			std::string end_label = make_label("L_logic_end_");
			emit_value(*binary.lhs, frame, slots);
			out_ << "\tcbz w0, " << false_label << "\n";
			emit_value(*binary.rhs, frame, slots);
			out_ << "\tcbz w0, " << false_label << "\n";
			out_ << "\tmov w0, #1\n";
			out_ << "\tb " << end_label << "\n";
			out_ << false_label << ":\n";
			out_ << "\tmov w0, #0\n";
			out_ << end_label << ":\n";
			return;
		}

		std::string true_label = make_label("L_logic_true_");
		std::string end_label = make_label("L_logic_end_");
		emit_value(*binary.lhs, frame, slots);
		out_ << "\tcbnz w0, " << true_label << "\n";
		emit_value(*binary.rhs, frame, slots);
		out_ << "\tcbnz w0, " << true_label << "\n";
		out_ << "\tmov w0, #0\n";
		out_ << "\tb " << end_label << "\n";
		out_ << true_label << ":\n";
		out_ << "\tmov w0, #1\n";
		out_ << end_label << ":\n";
	}

	void emit_call(const ir::CallValue &call, const Frame &frame, const SlotMap &slots)
	{
		for (auto it = call.arguments.rbegin(); it != call.arguments.rend(); ++it) {
			emit_value(**it, frame, slots);
			emit_push_accumulator();
		}

		std::size_t register_count =
			std::min(call.arguments.size(), argument_registers().size());
		for (std::size_t i = 0; i < register_count; ++i)
			emit_pop_to(argument_registers()[i]);

		out_ << "\tbl " << darwin_symbol(call.callee) << "\n";

		std::size_t stack_argument_count =
			call.arguments.size() > argument_registers().size()
				? call.arguments.size() - argument_registers().size()
				: 0;
		if (stack_argument_count > 0)
			out_ << "\tadd sp, sp, #" << stack_argument_count * 16 << "\n";
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
			collect_string_labels(
				*static_cast<const ir::LetStatement &>(statement).value);
		} else if (statement.kind == ir::Statement::Kind::Assign) {
			collect_string_labels(
				*static_cast<const ir::AssignStatement &>(statement).value);
		} else if (statement.kind == ir::Statement::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ir::IndirectAssignStatement &>(statement);
			collect_string_labels(*assign.target);
			collect_string_labels(*assign.value);
		} else if (statement.kind == ir::Statement::Kind::Expr) {
			const auto &expr = static_cast<const ir::ExprStatement &>(statement);
			collect_string_labels(*expr.value);
		} else if (statement.kind == ir::Statement::Kind::If) {
			const auto &if_statement = static_cast<const ir::IfStatement &>(statement);
			collect_string_labels(*if_statement.condition);
			for (const auto &branch_statement : if_statement.then_body)
				collect_string_labels(*branch_statement);
			for (const auto &branch_statement : if_statement.else_body)
				collect_string_labels(*branch_statement);
		} else if (statement.kind == ir::Statement::Kind::While) {
			const auto &while_statement = static_cast<const ir::WhileStatement &>(statement);
			collect_string_labels(*while_statement.condition);
			for (const auto &body_statement : while_statement.body)
				collect_string_labels(*body_statement);
		} else if (statement.kind == ir::Statement::Kind::For) {
			const auto &for_statement = static_cast<const ir::ForStatement &>(statement);
			collect_string_labels(*for_statement.initializer);
			collect_string_labels(*for_statement.condition);
			collect_string_labels(*for_statement.increment);
			for (const auto &body_statement : for_statement.body)
				collect_string_labels(*body_statement);
		} else if (statement.kind == ir::Statement::Kind::Return) {
			collect_string_labels(
				*static_cast<const ir::ReturnStatement &>(statement).value);
		}
	}

	void collect_string_labels(const ir::Value &value)
	{
		switch (value.kind) {
		case ir::Value::Kind::String: {
			const auto &string = static_cast<const ir::StringValue &>(value);
			string_labels_[&string] = "Lstr" + std::to_string(string_labels_.size());
			return;
		}
		case ir::Value::Kind::Unary:
			collect_string_labels(*static_cast<const ir::UnaryValue &>(value).operand);
			return;
		case ir::Value::Kind::Binary: {
			const auto &binary = static_cast<const ir::BinaryValue &>(value);
			collect_string_labels(*binary.lhs);
			collect_string_labels(*binary.rhs);
			return;
		}
		case ir::Value::Kind::Cast:
			collect_string_labels(*static_cast<const ir::CastValue &>(value).value);
			return;
		case ir::Value::Kind::Call: {
			const auto &call = static_cast<const ir::CallValue &>(value);
			for (const auto &argument : call.arguments)
				collect_string_labels(*argument);
			return;
		}
		case ir::Value::Kind::Integer:
		case ir::Value::Kind::Bool:
		case ir::Value::Kind::Char:
		case ir::Value::Kind::Local:
		case ir::Value::Kind::Global:
			return;
		}
	}

	void emit_string_section(const ir::Module &module)
	{
		if (string_labels_.empty() && !has_static_string_initializers(module))
			return;
		out_ << ".cstring\n";
		for (const auto &buffer : module.static_buffers)
			emit_static_initializer_string_literals(buffer);
		for (const auto &function : module.functions) {
			for (const auto &statement : function.body)
				emit_string_literals(*statement);
		}
	}

	void emit_static_buffer_section(const ir::Module &module)
	{
		for (const auto &buffer : module.static_buffers) {
			if (!buffer.initializers.empty()) {
				emit_static_array(buffer);
				continue;
			}
			out_ << ".zerofill __DATA,__bss," << static_buffer_label(buffer.name) << ","
			     << static_buffer_size_bytes(buffer) << ",4\n";
		}
	}

	bool has_static_string_initializers(const ir::Module &module) const
	{
		for (const auto &buffer : module.static_buffers) {
			for (const auto &initializer : buffer.initializers) {
				if (initializer.kind == ir::StaticBuffer::Initializer::Kind::String)
					return true;
			}
		}
		return false;
	}

	void emit_static_initializer_string_literals(const ir::StaticBuffer &buffer)
	{
		for (std::size_t i = 0; i < buffer.initializers.size(); ++i) {
			const auto &initializer = buffer.initializers[i];
			if (initializer.kind != ir::StaticBuffer::Initializer::Kind::String)
				continue;
			out_ << static_initializer_string_label(buffer.name, i) << ":\n";
			out_ << "\t.asciz \"" << escape_asciz_payload(initializer.literal) << "\"\n";
		}
	}

	void emit_static_array(const ir::StaticBuffer &buffer)
	{
		out_ << ".data\n";
		out_ << static_buffer_label(buffer.name) << ":\n";
		for (std::size_t i = 0; i < buffer.initializers.size(); ++i)
			emit_static_array_initializer(buffer, i, buffer.initializers[i]);
		std::size_t initialized_bytes =
			buffer.initializers.size() *
			static_cast<std::size_t>(std::max(1, memory_bits(buffer.element_type) / 8));
		std::size_t total_bytes = static_buffer_size_bytes(buffer);
		if (initialized_bytes < total_bytes)
			out_ << "\t.space " << (total_bytes - initialized_bytes) << "\n";
	}

	void emit_static_array_initializer(const ir::StaticBuffer &buffer, std::size_t index,
	                                   const ir::StaticBuffer::Initializer &initializer)
	{
		if (initializer.kind == ir::StaticBuffer::Initializer::Kind::String) {
			out_ << "\t.quad " << static_initializer_string_label(buffer.name, index) << "\n";
			return;
		}
		out_ << "\t" << scalar_directive(buffer.element_type) << " "
		     << static_initializer_literal(initializer) << "\n";
	}

	std::string static_initializer_literal(
		const ir::StaticBuffer::Initializer &initializer) const
	{
		switch (initializer.kind) {
		case ir::StaticBuffer::Initializer::Kind::Integer:
			return initializer.is_negative ? "-" + initializer.literal : initializer.literal;
		case ir::StaticBuffer::Initializer::Kind::Bool:
			return initializer.bool_value ? "1" : "0";
		case ir::StaticBuffer::Initializer::Kind::Char:
			return std::to_string(static_cast<unsigned int>(initializer.char_value));
		case ir::StaticBuffer::Initializer::Kind::String:
			break;
		}
		return "0";
	}

	void emit_static_scalar_section(const ir::Module &module)
	{
		if (module.static_scalars.empty())
			return;
		out_ << ".data\n";
		for (const auto &scalar : module.static_scalars) {
			out_ << static_scalar_label(scalar.name) << ":\n";
			out_ << "\t.long " << scalar.initializer_literal << "\n";
		}
	}

	std::string static_buffer_label(const std::string &name) const
	{
		return "Lstatic_" + name;
	}

	std::string static_scalar_label(const std::string &name) const
	{
		return "Lstatic_" + name;
	}

	std::string static_initializer_string_label(const std::string &name,
	                                            std::size_t index) const
	{
		return "Lstaticstr_" + name + "_" + std::to_string(index);
	}

	void emit_static_scalar_load(const std::string &name)
	{
		out_ << "\tadrp x8, " << static_scalar_label(name) << "@PAGE\n";
		out_ << "\tadd x8, x8, " << static_scalar_label(name) << "@PAGEOFF\n";
		out_ << "\tldr w0, [x8]\n";
	}

	void emit_static_scalar_store(const std::string &name)
	{
		out_ << "\tadrp x8, " << static_scalar_label(name) << "@PAGE\n";
		out_ << "\tadd x8, x8, " << static_scalar_label(name) << "@PAGEOFF\n";
		out_ << "\tstr w0, [x8]\n";
	}

	void emit_string_literals(const ir::Statement &statement)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			emit_string_literals(*static_cast<const ir::LetStatement &>(statement).value);
		} else if (statement.kind == ir::Statement::Kind::Assign) {
			emit_string_literals(*static_cast<const ir::AssignStatement &>(statement).value);
		} else if (statement.kind == ir::Statement::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ir::IndirectAssignStatement &>(statement);
			emit_string_literals(*assign.target);
			emit_string_literals(*assign.value);
		} else if (statement.kind == ir::Statement::Kind::Expr) {
			const auto &expr = static_cast<const ir::ExprStatement &>(statement);
			emit_string_literals(*expr.value);
		} else if (statement.kind == ir::Statement::Kind::If) {
			const auto &if_statement = static_cast<const ir::IfStatement &>(statement);
			emit_string_literals(*if_statement.condition);
			for (const auto &branch_statement : if_statement.then_body)
				emit_string_literals(*branch_statement);
			for (const auto &branch_statement : if_statement.else_body)
				emit_string_literals(*branch_statement);
		} else if (statement.kind == ir::Statement::Kind::While) {
			const auto &while_statement = static_cast<const ir::WhileStatement &>(statement);
			emit_string_literals(*while_statement.condition);
			for (const auto &body_statement : while_statement.body)
				emit_string_literals(*body_statement);
		} else if (statement.kind == ir::Statement::Kind::For) {
			const auto &for_statement = static_cast<const ir::ForStatement &>(statement);
			emit_string_literals(*for_statement.initializer);
			emit_string_literals(*for_statement.condition);
			emit_string_literals(*for_statement.increment);
			for (const auto &body_statement : for_statement.body)
				emit_string_literals(*body_statement);
		} else if (statement.kind == ir::Statement::Kind::Return) {
			emit_string_literals(*static_cast<const ir::ReturnStatement &>(statement).value);
		}
	}

	void emit_string_literals(const ir::Value &value)
	{
		switch (value.kind) {
		case ir::Value::Kind::String: {
			const auto &string = static_cast<const ir::StringValue &>(value);
			out_ << string_labels_.at(&string) << ":\n";
			out_ << "\t.asciz \"" << escape_asciz_payload(string.value) << "\"\n";
			return;
		}
		case ir::Value::Kind::Unary:
			emit_string_literals(*static_cast<const ir::UnaryValue &>(value).operand);
			return;
		case ir::Value::Kind::Binary: {
			const auto &binary = static_cast<const ir::BinaryValue &>(value);
			emit_string_literals(*binary.lhs);
			emit_string_literals(*binary.rhs);
			return;
		}
		case ir::Value::Kind::Cast:
			emit_string_literals(*static_cast<const ir::CastValue &>(value).value);
			return;
		case ir::Value::Kind::Call: {
			const auto &call = static_cast<const ir::CallValue &>(value);
			for (const auto &argument : call.arguments)
				emit_string_literals(*argument);
			return;
		}
		case ir::Value::Kind::Integer:
		case ir::Value::Kind::Bool:
		case ir::Value::Kind::Char:
		case ir::Value::Kind::Local:
		case ir::Value::Kind::Global:
			return;
		}
	}

	void emit_push_accumulator()
	{
		out_ << "\tsub sp, sp, #16\n";
		out_ << "\tstr x0, [sp]\n";
	}

	void emit_pop_to(const std::string &reg)
	{
		out_ << "\tldr " << reg << ", [sp]\n";
		out_ << "\tadd sp, sp, #16\n";
	}

	std::string make_label(const std::string &prefix)
	{
		return prefix + std::to_string(next_label_id_++);
	}

	std::string condition_code(const ir::BinaryValue &binary) const
	{
		if (binary.op == "==")
			return "eq";
		if (binary.op == "!=")
			return "ne";
		bool is_unsigned = is_unsigned_integer(binary.lhs->type) || is_pointer(binary.lhs->type);
		if (binary.op == "<")
			return is_unsigned ? "lo" : "lt";
		if (binary.op == "<=")
			return is_unsigned ? "ls" : "le";
		if (binary.op == ">")
			return is_unsigned ? "hi" : "gt";
		if (binary.op == ">=")
			return is_unsigned ? "hs" : "ge";
		throw std::runtime_error("unexpected comparison operator");
	}

	int memory_bits(ir::Type type) const
	{
		if (type.kind == PrimitiveKind::Bool || type.kind == PrimitiveKind::Char)
			return 8;
		if (is_integer(type))
			return type.bits;
		return 64;
	}

	const char *scalar_directive(ir::Type type) const
	{
		switch (memory_bits(type)) {
		case 8:
			return ".byte";
		case 16:
			return ".short";
		case 32:
			return ".long";
		default:
			return ".quad";
		}
	}

	int pointee_size_bytes(ir::Type pointer_type) const
	{
		auto target_type = pointee_type(pointer_type);
		if (!target_type)
			return 8;
		return std::max(1, memory_bits(*target_type) / 8);
	}

	std::size_t static_buffer_size_bytes(const ir::StaticBuffer &buffer) const
	{
		return buffer.length * static_cast<std::size_t>(
		                           std::max(1, memory_bits(buffer.element_type) / 8));
	}

	static const std::vector<std::string> &argument_registers()
	{
		static const std::vector<std::string> registers{
			"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
		return registers;
	}

	Diagnostics &diagnostics_;
	std::ostringstream out_;
	std::unordered_map<const ir::StringValue *, std::string> string_labels_;
	std::unordered_set<std::string> static_buffer_names_;
	std::vector<std::string> loop_start_labels_;
	std::vector<std::string> loop_end_labels_;
	int next_label_id_ = 0;
};

} // namespace

CodegenResult emit_arm64_macos_assembly(const ir::Module &module,
                                        Diagnostics &diagnostics)
{
	return Arm64Emitter(diagnostics).emit(module);
}

} // namespace rexc
