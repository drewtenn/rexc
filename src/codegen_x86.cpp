// GNU assembly backend for Rexy's typed IR.
//
// This file is the final compiler stage before an external assembler. It
// chooses an i386 or x86_64 calling convention, lays out stack slots, emits
// labels for branches/loops, materializes literals, and turns typed IR values
// into target instructions. It reports backend diagnostics for IR that is
// valid Rexy but unsupported by the selected machine target, such as 64-bit
// values on the current i386 path.
#include "rexc/codegen_x86.hpp"
#include "rexc/types.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <sstream>
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

int count_locals(const std::vector<std::unique_ptr<ir::Statement>> &statements)
{
	int count = 0;
	for (const auto &statement : statements) {
		if (statement->kind == ir::Statement::Kind::Let) {
			++count;
		} else if (statement->kind == ir::Statement::Kind::While) {
			const auto &while_statement = static_cast<const ir::WhileStatement &>(*statement);
			count += count_locals(while_statement.body);
		} else if (statement->kind == ir::Statement::Kind::If) {
			const auto &if_statement = static_cast<const ir::IfStatement &>(*statement);
			count += count_locals(if_statement.then_body);
			count += count_locals(if_statement.else_body);
		}
	}
	return count;
}

int count_locals(const ir::Function &function)
{
	return count_locals(function.body);
}

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

std::string unsupported_codegen_message(ir::Type type, CodegenTarget target)
{
	if (target == CodegenTarget::I386 && is_integer(type) && type.bits == 64)
		return "64-bit integer code generation is not implemented for i386";
	return format_type(type) + " code generation is not implemented for " +
	       (target == CodegenTarget::X86_64 ? "x86_64" : "i386");
}

class Emitter {
public:
	Emitter(Diagnostics &diagnostics, CodegenTarget target)
		: diagnostics_(diagnostics), target_(target)
	{
	}

	CodegenResult emit(const ir::Module &module)
	{
		std::size_t starting_diagnostics = diagnostics_.items().size();

		// Validate the entire module before emitting any assembly so failed
		// codegen cannot hand back plausible partial output.
		validate_module(module);
		if (diagnostics_.items().size() != starting_diagnostics)
			return CodegenResult(false, "");

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
	void emit_function(const ir::Function &function)
	{
		Frame frame = build_frame(function);
		std::string done_label = ".L_return_" + function.name;

		out_ << ".globl " << function.name << '\n';
		out_ << function.name << ":\n";
		if (target_ == CodegenTarget::X86_64) {
			out_ << "\tpushq %rbp\n";
			out_ << "\tmovq %rsp, %rbp\n";
		} else {
			out_ << "\tpushl %ebp\n";
			out_ << "\tmovl %esp, %ebp\n";
		}
		if (frame.local_bytes > 0)
			out_ << "\t" << stack_sub_instruction() << " $" << frame.local_bytes
			     << ", " << stack_pointer_register() << "\n";

		if (target_ == CodegenTarget::X86_64)
			emit_x86_64_parameter_spills(function, frame);

		SlotMap slots = frame.parameter_slots;
		for (const auto &statement : function.body)
			emit_statement(*statement, frame, done_label, slots);

		out_ << done_label << ":\n";
		out_ << "\tleave\n";
		out_ << "\tret\n\n";
	}

	Frame build_frame(const ir::Function &function)
	{
		if (target_ == CodegenTarget::X86_64)
			return build_x86_64_frame(function);

		Frame frame;
		int offset = 8;
		for (const auto &parameter : function.parameters) {
			frame.parameter_slots[parameter.name] = offset;
			offset += 4;
		}

		int local_index = 0;
		assign_i386_local_slots(function.body, frame, local_index);
		frame.local_bytes = count_locals(function) * 4;
		return frame;
	}

	Frame build_x86_64_frame(const ir::Function &function)
	{
		Frame frame;
		int slot_index = 0;
		for (std::size_t i = 0; i < function.parameters.size(); ++i) {
			const auto &parameter = function.parameters[i];
			if (i < x86_64_argument_registers().size()) {
				++slot_index;
				frame.parameter_slots[parameter.name] = -8 * slot_index;
			} else {
				frame.parameter_slots[parameter.name] =
					16 + static_cast<int>(i - x86_64_argument_registers().size()) * 8;
			}
		}

		assign_x86_64_local_slots(function.body, frame, slot_index);

		frame.local_bytes = align_stack_bytes(slot_index * 8);
		return frame;
	}

	void assign_i386_local_slots(const std::vector<std::unique_ptr<ir::Statement>> &statements,
	                             Frame &frame, int &local_index)
	{
		for (const auto &statement : statements) {
			if (statement->kind == ir::Statement::Kind::Let) {
				const auto &let = static_cast<const ir::LetStatement &>(*statement);
				++local_index;
				frame.let_slots[&let] = -4 * local_index;
				continue;
			}
			if (statement->kind == ir::Statement::Kind::While) {
				const auto &while_statement = static_cast<const ir::WhileStatement &>(*statement);
				assign_i386_local_slots(while_statement.body, frame, local_index);
				continue;
			}
			if (statement->kind == ir::Statement::Kind::If) {
				const auto &if_statement = static_cast<const ir::IfStatement &>(*statement);
				assign_i386_local_slots(if_statement.then_body, frame, local_index);
				assign_i386_local_slots(if_statement.else_body, frame, local_index);
			}
		}
	}

	void assign_x86_64_local_slots(
		const std::vector<std::unique_ptr<ir::Statement>> &statements, Frame &frame,
		int &slot_index)
	{
		for (const auto &statement : statements) {
			if (statement->kind == ir::Statement::Kind::Let) {
				const auto &let = static_cast<const ir::LetStatement &>(*statement);
				++slot_index;
				frame.let_slots[&let] = -8 * slot_index;
				continue;
			}
			if (statement->kind == ir::Statement::Kind::While) {
				const auto &while_statement = static_cast<const ir::WhileStatement &>(*statement);
				assign_x86_64_local_slots(while_statement.body, frame, slot_index);
				continue;
			}
			if (statement->kind == ir::Statement::Kind::If) {
				const auto &if_statement = static_cast<const ir::IfStatement &>(*statement);
				assign_x86_64_local_slots(if_statement.then_body, frame, slot_index);
				assign_x86_64_local_slots(if_statement.else_body, frame, slot_index);
			}
		}
	}

	bool validate_module(const ir::Module &module)
	{
		bool ok = true;
		for (const auto &buffer : module.static_buffers)
			ok = validate_type(buffer.element_type) && ok;
		for (const auto &scalar : module.static_scalars)
			ok = validate_type(scalar.type) && ok;
		for (const auto &function : module.functions)
			ok = validate_function(function) && ok;
		return ok;
	}

	bool validate_function(const ir::Function &function)
	{
		current_function_ = function.name;

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

		if (statement.kind == ir::Statement::Kind::Assign) {
			const auto &assign = static_cast<const ir::AssignStatement &>(statement);
			return validate_value(*assign.value);
		}

		if (statement.kind == ir::Statement::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ir::IndirectAssignStatement &>(statement);
			bool ok = validate_value(*assign.target);
			ok = validate_value(*assign.value) && ok;
			return ok;
		}

		if (statement.kind == ir::Statement::Kind::Expr) {
			const auto &expr = static_cast<const ir::ExprStatement &>(statement);
			return validate_value(*expr.value);
		}

		if (statement.kind == ir::Statement::Kind::If) {
			const auto &if_statement = static_cast<const ir::IfStatement &>(statement);
			bool ok = validate_value(*if_statement.condition);
			for (const auto &branch_statement : if_statement.then_body)
				ok = validate_statement(*branch_statement) && ok;
			for (const auto &branch_statement : if_statement.else_body)
				ok = validate_statement(*branch_statement) && ok;
			return ok;
		}

		if (statement.kind == ir::Statement::Kind::While) {
			const auto &while_statement = static_cast<const ir::WhileStatement &>(statement);
			bool ok = validate_value(*while_statement.condition);
			for (const auto &body_statement : while_statement.body)
				ok = validate_statement(*body_statement) && ok;
			return ok;
		}

		if (statement.kind == ir::Statement::Kind::Break ||
		    statement.kind == ir::Statement::Kind::Continue)
			return true;

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
		case ir::Value::Kind::Global:
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
		case ir::Value::Kind::Cast: {
			const auto &cast = static_cast<const ir::CastValue &>(value);
			return validate_value(*cast.value) && ok;
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
		if (is_target_codegen_supported(type))
			return true;
		std::string message = unsupported_codegen_message(type, target_);
		std::string key = current_function_ + '\n' + message;
		// Report one unsupported-backend diagnostic per function to avoid
		// spam from return type, locals, and return value all repeating it.
		if (unsupported_diagnostics_.insert(std::move(key)).second)
			diagnostics_.error({}, std::move(message));
		return false;
	}

	void emit_statement(const ir::Statement &statement, const Frame &frame,
	                    const std::string &done_label, SlotMap &slots)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			const auto &let = static_cast<const ir::LetStatement &>(statement);
			emit_value(*let.value, frame, slots);
			int slot = frame.let_slots.at(&let);
			out_ << "\t" << move_instruction() << " " << accumulator_register() << ", "
			     << slot << "(" << frame_pointer_register() << ")\n";
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
			out_ << "\t" << move_instruction() << " " << accumulator_register() << ", "
			     << slots.at(assign.name) << "(" << frame_pointer_register() << ")\n";
			return;
		}

		if (statement.kind == ir::Statement::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ir::IndirectAssignStatement &>(statement);
			emit_indirect_assign(assign, frame, slots);
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

		if (statement.kind == ir::Statement::Kind::Break) {
			out_ << "\tjmp " << loop_end_labels_.back() << "\n";
			return;
		}

		if (statement.kind == ir::Statement::Kind::Continue) {
			out_ << "\tjmp " << loop_start_labels_.back() << "\n";
			return;
		}

		const auto &ret = static_cast<const ir::ReturnStatement &>(statement);
		emit_value(*ret.value, frame, slots);
		out_ << "\tjmp " << done_label << '\n';
	}

	void emit_if_statement(const ir::IfStatement &if_statement, const Frame &frame,
	                       const std::string &done_label, const SlotMap &slots)
	{
		std::string else_label = make_label(".L_else_");
		std::string end_label = make_label(".L_end_if_");

		emit_value(*if_statement.condition, frame, slots);
		out_ << "\tcmpb $0, %al\n";
		out_ << "\tje " << else_label << "\n";

		SlotMap then_slots = slots;
		for (const auto &statement : if_statement.then_body)
			emit_statement(*statement, frame, done_label, then_slots);
		out_ << "\tjmp " << end_label << "\n";

		out_ << else_label << ":\n";
		SlotMap else_slots = slots;
		for (const auto &statement : if_statement.else_body)
			emit_statement(*statement, frame, done_label, else_slots);

		out_ << end_label << ":\n";
	}

	void emit_while_statement(const ir::WhileStatement &while_statement, const Frame &frame,
	                          const std::string &done_label, const SlotMap &slots)
	{
		std::string start_label = make_label(".L_while_start_");
		std::string end_label = make_label(".L_while_end_");

		out_ << start_label << ":\n";
		emit_value(*while_statement.condition, frame, slots);
		out_ << "\tcmpb $0, %al\n";
		out_ << "\tje " << end_label << "\n";

		loop_start_labels_.push_back(start_label);
		loop_end_labels_.push_back(end_label);
		SlotMap body_slots = slots;
		for (const auto &statement : while_statement.body)
			emit_statement(*statement, frame, done_label, body_slots);
		loop_end_labels_.pop_back();
		loop_start_labels_.pop_back();
		out_ << "\tjmp " << start_label << "\n";
		out_ << end_label << ":\n";
	}

	void emit_value(const ir::Value &value, const Frame &frame, const SlotMap &slots)
	{
		switch (value.kind) {
		case ir::Value::Kind::Integer: {
			const auto &integer = static_cast<const ir::IntegerValue &>(value);
			out_ << "\t" << move_immediate_instruction() << " $";
			if (integer.is_negative)
				out_ << '-';
			out_ << canonical_decimal_literal(integer.literal) << ", "
			     << accumulator_register() << "\n";
			return;
		}
		case ir::Value::Kind::Bool: {
			const auto &boolean = static_cast<const ir::BoolValue &>(value);
			out_ << "\t" << move_instruction() << " $" << (boolean.value ? 1 : 0)
			     << ", " << accumulator_register() << "\n";
			return;
		}
		case ir::Value::Kind::Char: {
			const auto &character = static_cast<const ir::CharValue &>(value);
			out_ << "\t" << move_instruction() << " $"
			     << static_cast<unsigned int>(character.value) << ", "
			     << accumulator_register() << "\n";
			return;
		}
		case ir::Value::Kind::String: {
			const auto &string = static_cast<const ir::StringValue &>(value);
			if (target_ == CodegenTarget::X86_64) {
				out_ << "\tleaq " << string_labels_.at(&string) << "(%rip), %rax\n";
			} else {
				out_ << "\t" << move_instruction() << " $" << string_labels_.at(&string)
				     << ", " << accumulator_register() << "\n";
			}
			return;
		}
		case ir::Value::Kind::Global: {
			const auto &global = static_cast<const ir::GlobalValue &>(value);
			if (global.type.kind != PrimitiveKind::Str) {
				emit_static_scalar_load(global.name);
				return;
			}
			if (target_ == CodegenTarget::X86_64) {
				out_ << "\tleaq " << static_buffer_label(global.name) << "(%rip), %rax\n";
			} else {
				out_ << "\t" << move_instruction() << " $" << static_buffer_label(global.name)
				     << ", " << accumulator_register() << "\n";
			}
			return;
		}
		case ir::Value::Kind::Unary:
			emit_unary(static_cast<const ir::UnaryValue &>(value), frame, slots);
			return;
		case ir::Value::Kind::Local: {
			const auto &local = static_cast<const ir::LocalValue &>(value);
			out_ << "\t" << move_instruction() << " " << slots.at(local.name)
			     << "(" << frame_pointer_register() << "), " << accumulator_register()
			     << "\n";
			return;
		}
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

	void emit_cast(const ir::CastValue &cast, const Frame &frame, const SlotMap &slots)
	{
		emit_value(*cast.value, frame, slots);
		if (!is_integer(cast.type))
			return;

		if (cast.type.bits == 8) {
			out_ << "\t" << (is_signed_integer(cast.type) ? sign_extend_8_instruction()
			                                             : zero_extend_8_instruction())
			     << "\n";
			return;
		}
		if (cast.type.bits == 16) {
			out_ << "\t" << (is_signed_integer(cast.type) ? sign_extend_16_instruction()
			                                             : zero_extend_16_instruction())
			     << "\n";
			return;
		}
		if (target_ == CodegenTarget::X86_64 && cast.type.bits == 32) {
			out_ << "\t" << (is_signed_integer(cast.type) ? "movslq %eax, %rax"
			                                             : "movl %eax, %eax")
			     << "\n";
		}
	}

	void emit_unary(const ir::UnaryValue &unary, const Frame &frame, const SlotMap &slots)
	{
		if (unary.op == "&") {
			emit_address_of(*unary.operand, slots);
			return;
		}

		emit_value(*unary.operand, frame, slots);
		if (unary.op == "-") {
			out_ << "\t" << negate_instruction() << " " << accumulator_register() << "\n";
		} else if (unary.op == "!") {
			out_ << "\tcmpb $0, %al\n";
			out_ << "\tsete %al\n";
			out_ << "\t" << zero_extend_bool_instruction() << " %al, "
			     << accumulator_register() << "\n";
		} else if (unary.op == "*") {
			emit_indirect_load(unary.type);
		}
	}

	void emit_address_of(const ir::Value &operand, const SlotMap &slots)
	{
		const auto &local = static_cast<const ir::LocalValue &>(operand);
		out_ << "\t" << load_effective_address_instruction() << " "
		     << slots.at(local.name) << "(" << frame_pointer_register() << "), "
		     << accumulator_register() << "\n";
	}

	void emit_indirect_assign(const ir::IndirectAssignStatement &assign,
	                          const Frame &frame, const SlotMap &slots)
	{
		emit_value(*assign.value, frame, slots);
		emit_push_accumulator();
		emit_value(*assign.target, frame, slots);
		emit_pop_scratch();
		emit_indirect_store(assign.value->type);
	}

	void emit_indirect_load(ir::Type type)
	{
		out_ << "\t" << load_indirect_instruction(type) << " ("
		     << accumulator_register() << "), " << load_destination_register(type) << "\n";
	}

	void emit_indirect_store(ir::Type type)
	{
		out_ << "\t" << store_instruction(type) << " " << store_source_register(type)
		     << ", (" << accumulator_register() << ")\n";
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
		out_ << "\t" << move_instruction() << " " << accumulator_register() << ", "
		     << scratch_register() << "\n";
		emit_pop_accumulator();
		scale_pointer_offset(binary);

		if (is_comparison_operator(binary.op)) {
			out_ << "\t" << compare_instruction() << " " << scratch_register() << ", "
			     << accumulator_register() << "\n";
			out_ << "\t" << setcc_instruction(binary) << " %al\n";
			out_ << "\t" << zero_extend_bool_instruction() << " %al, "
			     << accumulator_register() << "\n";
		} else if (binary.op == "+")
			out_ << "\t" << add_instruction() << " " << scratch_register() << ", "
			     << accumulator_register() << "\n";
		else if (binary.op == "-")
			out_ << "\t" << subtract_instruction() << " " << scratch_register() << ", "
			     << accumulator_register() << "\n";
		else if (binary.op == "*")
			out_ << "\t" << multiply_instruction() << " " << scratch_register() << ", "
			     << accumulator_register() << "\n";
		else if (binary.op == "/") {
			if (is_unsigned_integer(binary.type)) {
				out_ << "\t" << zero_remainder_instruction() << " "
				     << remainder_register() << ", " << remainder_register() << "\n";
				out_ << "\t" << unsigned_divide_instruction() << " "
				     << scratch_register() << "\n";
			} else {
				out_ << "\t" << sign_extend_dividend_instruction() << "\n";
				out_ << "\t" << signed_divide_instruction() << " "
				     << scratch_register() << "\n";
			}
		} else if (binary.op == "%") {
			if (is_unsigned_integer(binary.type)) {
				out_ << "\t" << zero_remainder_instruction() << " "
				     << remainder_register() << ", " << remainder_register() << "\n";
				out_ << "\t" << unsigned_divide_instruction() << " "
				     << scratch_register() << "\n";
			} else {
				out_ << "\t" << sign_extend_dividend_instruction() << "\n";
				out_ << "\t" << signed_divide_instruction() << " "
				     << scratch_register() << "\n";
			}
			out_ << "\t" << move_instruction() << " " << remainder_register() << ", "
			     << accumulator_register() << "\n";
		}
	}

	void scale_pointer_offset(const ir::BinaryValue &binary)
	{
		if (!is_pointer(binary.type) || (binary.op != "+" && binary.op != "-"))
			return;
		int scale = pointee_size_bytes(binary.type);
		if (scale == 1)
			return;
		out_ << "\t" << multiply_instruction() << " $" << scale << ", "
		     << scratch_register() << "\n";
	}

	void emit_logical_binary(const ir::BinaryValue &binary, const Frame &frame,
	                         const SlotMap &slots)
	{
		if (binary.op == "&&") {
			emit_logical_and(binary, frame, slots);
			return;
		}
		emit_logical_or(binary, frame, slots);
	}

	void emit_logical_and(const ir::BinaryValue &binary, const Frame &frame,
	                      const SlotMap &slots)
	{
		std::string false_label = make_label(".L_logic_false_");
		std::string end_label = make_label(".L_logic_end_");

		emit_value(*binary.lhs, frame, slots);
		out_ << "\tcmpb $0, %al\n";
		out_ << "\tje " << false_label << "\n";
		emit_value(*binary.rhs, frame, slots);
		out_ << "\tcmpb $0, %al\n";
		out_ << "\tje " << false_label << "\n";
		out_ << "\t" << move_instruction() << " $1, " << accumulator_register() << "\n";
		out_ << "\tjmp " << end_label << "\n";
		out_ << false_label << ":\n";
		out_ << "\t" << move_instruction() << " $0, " << accumulator_register() << "\n";
		out_ << end_label << ":\n";
	}

	void emit_logical_or(const ir::BinaryValue &binary, const Frame &frame,
	                     const SlotMap &slots)
	{
		std::string true_label = make_label(".L_logic_true_");
		std::string end_label = make_label(".L_logic_end_");

		emit_value(*binary.lhs, frame, slots);
		out_ << "\tcmpb $0, %al\n";
		out_ << "\tjne " << true_label << "\n";
		emit_value(*binary.rhs, frame, slots);
		out_ << "\tcmpb $0, %al\n";
		out_ << "\tjne " << true_label << "\n";
		out_ << "\t" << move_instruction() << " $0, " << accumulator_register() << "\n";
		out_ << "\tjmp " << end_label << "\n";
		out_ << true_label << ":\n";
		out_ << "\t" << move_instruction() << " $1, " << accumulator_register() << "\n";
		out_ << end_label << ":\n";
	}

	void emit_call(const ir::CallValue &call, const Frame &frame, const SlotMap &slots)
	{
		if (target_ == CodegenTarget::X86_64) {
			emit_x86_64_call(call, frame, slots);
			return;
		}

		for (auto it = call.arguments.rbegin(); it != call.arguments.rend(); ++it) {
			emit_value(**it, frame, slots);
			out_ << "\tpushl %eax\n";
		}

		out_ << "\tcall " << call.callee << '\n';
		if (!call.arguments.empty())
			out_ << "\taddl $" << call.arguments.size() * 4 << ", %esp\n";
	}

	void emit_x86_64_call(const ir::CallValue &call, const Frame &frame,
	                      const SlotMap &slots)
	{
		std::size_t stack_argument_count =
			call.arguments.size() > x86_64_argument_registers().size()
				? call.arguments.size() - x86_64_argument_registers().size()
				: 0;
		bool needs_padding = stack_argument_count % 2 != 0;
		if (needs_padding) {
			out_ << "\tsubq $8, %rsp\n";
			eval_stack_bytes_ += 8;
		}

		for (auto it = call.arguments.rbegin(); it != call.arguments.rend(); ++it) {
			emit_value(**it, frame, slots);
			emit_push_accumulator();
		}

		std::size_t register_count =
			std::min(call.arguments.size(), x86_64_argument_registers().size());
		for (std::size_t i = 0; i < register_count; ++i)
			emit_pop_to(x86_64_argument_registers()[i]);

		out_ << "\tcall " << call.callee << '\n';

		std::size_t cleanup_bytes = stack_argument_count * 8 + (needs_padding ? 8 : 0);
		if (cleanup_bytes > 0) {
			out_ << "\taddq $" << cleanup_bytes << ", %rsp\n";
			eval_stack_bytes_ -= cleanup_bytes;
		}
	}

	void emit_x86_64_parameter_spills(const ir::Function &function, const Frame &frame)
	{
		std::size_t register_count =
			std::min(function.parameters.size(), x86_64_argument_registers().size());
		for (std::size_t i = 0; i < register_count; ++i) {
			out_ << "\tmovq " << x86_64_argument_registers()[i] << ", "
			     << frame.parameter_slots.at(function.parameters[i].name) << "(%rbp)\n";
		}
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

		if (statement.kind == ir::Statement::Kind::Assign) {
			const auto &assign = static_cast<const ir::AssignStatement &>(statement);
			collect_string_labels(*assign.value);
			return;
		}

		if (statement.kind == ir::Statement::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ir::IndirectAssignStatement &>(statement);
			collect_string_labels(*assign.target);
			collect_string_labels(*assign.value);
			return;
		}

		if (statement.kind == ir::Statement::Kind::Expr) {
			const auto &expr = static_cast<const ir::ExprStatement &>(statement);
			collect_string_labels(*expr.value);
			return;
		}

		if (statement.kind == ir::Statement::Kind::If) {
			const auto &if_statement = static_cast<const ir::IfStatement &>(statement);
			collect_string_labels(*if_statement.condition);
			for (const auto &branch_statement : if_statement.then_body)
				collect_string_labels(*branch_statement);
			for (const auto &branch_statement : if_statement.else_body)
				collect_string_labels(*branch_statement);
			return;
		}

		if (statement.kind == ir::Statement::Kind::While) {
			const auto &while_statement = static_cast<const ir::WhileStatement &>(statement);
			collect_string_labels(*while_statement.condition);
			for (const auto &body_statement : while_statement.body)
				collect_string_labels(*body_statement);
			return;
		}

		if (statement.kind == ir::Statement::Kind::Break ||
		    statement.kind == ir::Statement::Kind::Continue)
			return;

		const auto &ret = static_cast<const ir::ReturnStatement &>(statement);
		collect_string_labels(*ret.value);
	}

	void collect_string_labels(const ir::Value &value)
	{
		// Labels are keyed by IR node address so repeated equal string text
		// still has deterministic, unique labels in emission order.
		switch (value.kind) {
		case ir::Value::Kind::Integer:
		case ir::Value::Kind::Bool:
		case ir::Value::Kind::Char:
		case ir::Value::Kind::Local:
		case ir::Value::Kind::Global:
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
		case ir::Value::Kind::Cast: {
			const auto &cast = static_cast<const ir::CastValue &>(value);
			collect_string_labels(*cast.value);
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

	void emit_static_buffer_section(const ir::Module &module)
	{
		if (module.static_buffers.empty())
			return;
		out_ << ".section .bss\n";
		for (const auto &buffer : module.static_buffers) {
			out_ << static_buffer_label(buffer.name) << ":\n";
			out_ << "\t.zero " << buffer.length << "\n";
		}
	}

	void emit_static_scalar_section(const ir::Module &module)
	{
		if (module.static_scalars.empty())
			return;
		out_ << ".section .data\n";
		for (const auto &scalar : module.static_scalars) {
			out_ << static_scalar_label(scalar.name) << ":\n";
			out_ << "\t.long " << scalar.initializer_literal << "\n";
		}
	}

	std::string static_buffer_label(const std::string &name) const
	{
		return ".Lstatic_" + name;
	}

	std::string static_scalar_label(const std::string &name) const
	{
		return ".Lstatic_" + name;
	}

	void emit_static_scalar_load(const std::string &name)
	{
		if (target_ == CodegenTarget::X86_64) {
			out_ << "\tmovl " << static_scalar_label(name) << "(%rip), %eax\n";
			return;
		}
		out_ << "\tmovl " << static_scalar_label(name) << ", %eax\n";
	}

	void emit_static_scalar_store(const std::string &name)
	{
		if (target_ == CodegenTarget::X86_64) {
			out_ << "\tmovl %eax, " << static_scalar_label(name) << "(%rip)\n";
			return;
		}
		out_ << "\tmovl %eax, " << static_scalar_label(name) << "\n";
	}

	void emit_string_literals(const ir::Statement &statement)
	{
		if (statement.kind == ir::Statement::Kind::Let) {
			const auto &let = static_cast<const ir::LetStatement &>(statement);
			emit_string_literals(*let.value);
			return;
		}

		if (statement.kind == ir::Statement::Kind::Assign) {
			const auto &assign = static_cast<const ir::AssignStatement &>(statement);
			emit_string_literals(*assign.value);
			return;
		}

		if (statement.kind == ir::Statement::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ir::IndirectAssignStatement &>(statement);
			emit_string_literals(*assign.target);
			emit_string_literals(*assign.value);
			return;
		}

		if (statement.kind == ir::Statement::Kind::Expr) {
			const auto &expr = static_cast<const ir::ExprStatement &>(statement);
			emit_string_literals(*expr.value);
			return;
		}

		if (statement.kind == ir::Statement::Kind::If) {
			const auto &if_statement = static_cast<const ir::IfStatement &>(statement);
			emit_string_literals(*if_statement.condition);
			for (const auto &branch_statement : if_statement.then_body)
				emit_string_literals(*branch_statement);
			for (const auto &branch_statement : if_statement.else_body)
				emit_string_literals(*branch_statement);
			return;
		}

		if (statement.kind == ir::Statement::Kind::While) {
			const auto &while_statement = static_cast<const ir::WhileStatement &>(statement);
			emit_string_literals(*while_statement.condition);
			for (const auto &body_statement : while_statement.body)
				emit_string_literals(*body_statement);
			return;
		}

		if (statement.kind == ir::Statement::Kind::Break ||
		    statement.kind == ir::Statement::Kind::Continue)
			return;

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
		case ir::Value::Kind::Global:
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
		case ir::Value::Kind::Cast: {
			const auto &cast = static_cast<const ir::CastValue &>(value);
			emit_string_literals(*cast.value);
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
	CodegenTarget target_;
	std::ostringstream out_;
	std::string current_function_;
	std::unordered_map<const ir::StringValue *, std::string> string_labels_;
	std::unordered_set<std::string> unsupported_diagnostics_;
	std::vector<std::string> loop_start_labels_;
	std::vector<std::string> loop_end_labels_;
	int eval_stack_bytes_ = 0;
	int next_label_id_ = 0;

	bool is_target_codegen_supported(ir::Type type) const
	{
		if (target_ == CodegenTarget::X86_64)
			return is_valid_primitive_type(type);
		return is_i386_codegen_supported(type);
	}

	static int align_stack_bytes(int bytes)
	{
		return bytes == 0 ? 0 : ((bytes + 15) / 16) * 16;
	}

	static const std::vector<std::string> &x86_64_argument_registers()
	{
		static const std::vector<std::string> registers{
			"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
		return registers;
	}

	const char *stack_sub_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "subq" : "subl";
	}

	const char *load_effective_address_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "leaq" : "leal";
	}

	const char *move_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "movq" : "movl";
	}

	const char *move_immediate_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "movabsq" : "movl";
	}

	const char *add_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "addq" : "addl";
	}

	const char *subtract_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "subq" : "subl";
	}

	const char *multiply_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "imulq" : "imull";
	}

	const char *compare_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "cmpq" : "cmpl";
	}

	const char *zero_extend_bool_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "movzbq" : "movzbl";
	}

	const char *zero_extend_8_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "movzbq %al, %rax" : "movzbl %al, %eax";
	}

	const char *sign_extend_8_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "movsbq %al, %rax" : "movsbl %al, %eax";
	}

	const char *zero_extend_16_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "movzwq %ax, %rax" : "movzwl %ax, %eax";
	}

	const char *sign_extend_16_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "movswq %ax, %rax" : "movswl %ax, %eax";
	}

	const char *setcc_instruction(const ir::BinaryValue &binary) const
	{
		bool unsigned_operands = is_unsigned_integer(binary.lhs->type);
		if (binary.op == "==")
			return "sete";
		if (binary.op == "!=")
			return "setne";
		if (binary.op == "<")
			return unsigned_operands ? "setb" : "setl";
		if (binary.op == "<=")
			return unsigned_operands ? "setbe" : "setle";
		if (binary.op == ">")
			return unsigned_operands ? "seta" : "setg";
		if (binary.op == ">=")
			return unsigned_operands ? "setae" : "setge";
		throw std::runtime_error("unexpected comparison operator in x86 codegen");
	}

	const char *negate_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "negq" : "negl";
	}

	const char *unsigned_divide_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "divq" : "divl";
	}

	const char *signed_divide_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "idivq" : "idivl";
	}

	const char *sign_extend_dividend_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "cqto" : "cltd";
	}

	const char *zero_remainder_instruction() const
	{
		return target_ == CodegenTarget::X86_64 ? "xorq" : "xorl";
	}

	std::string load_indirect_instruction(ir::Type type) const
	{
		int bits = memory_bits(type);
		if (target_ == CodegenTarget::X86_64) {
			if (bits == 8)
				return is_signed_integer(type) ? "movsbq" : "movzbq";
			if (bits == 16)
				return is_signed_integer(type) ? "movswq" : "movzwq";
			if (bits == 32)
				return is_signed_integer(type) ? "movslq" : "movl";
			return "movq";
		}

		if (bits == 8)
			return is_signed_integer(type) ? "movsbl" : "movzbl";
		if (bits == 16)
			return is_signed_integer(type) ? "movswl" : "movzwl";
		return "movl";
	}

	const char *load_destination_register(ir::Type type) const
	{
		if (target_ == CodegenTarget::X86_64 && memory_bits(type) == 32 &&
		    !is_signed_integer(type))
			return "%eax";
		return accumulator_register();
	}

	const char *store_instruction(ir::Type type) const
	{
		switch (memory_bits(type)) {
		case 8:
			return "movb";
		case 16:
			return "movw";
		case 64:
			return "movq";
		default:
			return "movl";
		}
	}

	const char *store_source_register(ir::Type type) const
	{
		switch (memory_bits(type)) {
		case 8:
			return "%cl";
		case 16:
			return "%cx";
		case 64:
			return "%rcx";
		default:
			return "%ecx";
		}
	}

	int memory_bits(ir::Type type) const
	{
		if (is_integer(type))
			return type.bits;
		if (type.kind == PrimitiveKind::Bool)
			return 8;
		if (type.kind == PrimitiveKind::Char)
			return 32;
		if (type.kind == PrimitiveKind::Str || is_pointer(type))
			return target_ == CodegenTarget::X86_64 ? 64 : 32;
		return target_ == CodegenTarget::X86_64 ? 64 : 32;
	}

	int pointee_size_bytes(ir::Type pointer_type) const
	{
		auto target_type = pointee_type(pointer_type);
		if (!target_type)
			return 1;
		return memory_bits(*target_type) / 8;
	}

	const char *accumulator_register() const
	{
		return target_ == CodegenTarget::X86_64 ? "%rax" : "%eax";
	}

	const char *scratch_register() const
	{
		return target_ == CodegenTarget::X86_64 ? "%rcx" : "%ecx";
	}

	const char *remainder_register() const
	{
		return target_ == CodegenTarget::X86_64 ? "%rdx" : "%edx";
	}

	const char *frame_pointer_register() const
	{
		return target_ == CodegenTarget::X86_64 ? "%rbp" : "%ebp";
	}

	const char *stack_pointer_register() const
	{
		return target_ == CodegenTarget::X86_64 ? "%rsp" : "%esp";
	}

	void emit_push_accumulator()
	{
		if (target_ == CodegenTarget::X86_64) {
			out_ << "\tpushq %rax\n";
			eval_stack_bytes_ += 8;
		} else {
			out_ << "\tpushl %eax\n";
		}
	}

	void emit_pop_accumulator()
	{
		if (target_ == CodegenTarget::X86_64) {
			out_ << "\tpopq %rax\n";
			eval_stack_bytes_ -= 8;
		} else {
			out_ << "\tpopl %eax\n";
		}
	}

	void emit_pop_scratch()
	{
		if (target_ == CodegenTarget::X86_64) {
			out_ << "\tpopq %rcx\n";
			eval_stack_bytes_ -= 8;
		} else {
			out_ << "\tpopl %ecx\n";
		}
	}

	void emit_pop_to(const std::string &register_name)
	{
		out_ << "\tpopq " << register_name << "\n";
		eval_stack_bytes_ -= 8;
	}

	std::string make_label(const char *prefix)
	{
		return std::string(prefix) + std::to_string(next_label_id_++);
	}
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

CodegenResult emit_x86_assembly(const ir::Module &module, Diagnostics &diagnostics,
                                CodegenTarget target)
{
	Emitter emitter(diagnostics, target);
	return emitter.emit(module);
}

} // namespace rexc
