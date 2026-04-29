#include "rexc/codegen.hpp"
#include "rexc/stdlib.hpp"
#include "rexc/target.hpp"
#include "rexc/types.hpp"
#include "test_support.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

bool contains(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE(stdlib_uses_rx_files_as_canonical_source)
{
	const std::string source_dir = REXC_SOURCE_DIR;
	std::ifstream glue(source_dir + "/src/stdlib/stdlib.cpp");
	std::ifstream core_catalog(source_dir + "/src/stdlib/core/library.cpp");
	std::ifstream core_catalog_header(source_dir + "/src/stdlib/core/library.hpp");
	std::ifstream alloc_catalog(source_dir + "/src/stdlib/alloc/library.cpp");
	std::ifstream alloc_catalog_header(source_dir + "/src/stdlib/alloc/library.hpp");
	std::ifstream std_catalog(source_dir + "/src/stdlib/std/library.cpp");
	std::ifstream std_catalog_header(source_dir + "/src/stdlib/std/library.hpp");
	REQUIRE(glue.is_open());
	REQUIRE(!core_catalog.is_open());
	REQUIRE(!core_catalog_header.is_open());
	REQUIRE(!alloc_catalog.is_open());
	REQUIRE(!alloc_catalog_header.is_open());
	REQUIRE(!std_catalog.is_open());
	REQUIRE(!std_catalog_header.is_open());
	std::ostringstream glue_text;
	glue_text << glue.rdbuf();
	REQUIRE(!contains(glue_text.str(), "const char *portable_stdlib_source()"));

	std::ifstream core_str(source_dir + "/src/stdlib/core/str.rx");
	std::ifstream core_mem(source_dir + "/src/stdlib/core/mem.rx");
	std::ifstream core_num(source_dir + "/src/stdlib/core/num.rx");
	std::ifstream core_option(source_dir + "/src/stdlib/core/option.rx");
	std::ifstream core_result(source_dir + "/src/stdlib/core/result.rx");
	std::ifstream alloc(source_dir + "/src/stdlib/alloc/alloc.rx");
	std::ifstream std_io(source_dir + "/src/stdlib/std/io.rx");
	std::ifstream std_process(source_dir + "/src/stdlib/std/process.rx");
	std::ifstream std_fs(source_dir + "/src/stdlib/std/fs.rx");
	std::ifstream std_path(source_dir + "/src/stdlib/std/path.rx");
	std::ifstream std_time(source_dir + "/src/stdlib/std/time.rx");
	std::ifstream i386_linux_runtime(source_dir + "/src/stdlib/sys/runtime_i386_linux.cpp");
	std::ifstream i386_drunix_runtime(source_dir + "/src/stdlib/sys/runtime_i386_drunix.cpp");
	std::ifstream x86_64_linux_runtime(source_dir + "/src/stdlib/sys/runtime_x86_64_linux.cpp");
	std::ifstream arm64_macos_runtime(source_dir + "/src/stdlib/sys/runtime_arm64_macos.cpp");
	std::ifstream old_i386_runtime(source_dir + "/src/stdlib/sys/runtime_i386.cpp");
	std::ifstream old_x86_64_runtime(source_dir + "/src/stdlib/sys/runtime_x86_64.cpp");
	REQUIRE(core_str.is_open());
	REQUIRE(core_mem.is_open());
	REQUIRE(core_num.is_open());
	REQUIRE(core_option.is_open());
	REQUIRE(core_result.is_open());
	REQUIRE(alloc.is_open());
	REQUIRE(std_io.is_open());
	REQUIRE(std_process.is_open());
	REQUIRE(std_fs.is_open());
	REQUIRE(std_path.is_open());
	REQUIRE(std_time.is_open());
	REQUIRE(i386_linux_runtime.is_open());
	REQUIRE(i386_drunix_runtime.is_open());
	REQUIRE(x86_64_linux_runtime.is_open());
	REQUIRE(arm64_macos_runtime.is_open());
	REQUIRE(!old_i386_runtime.is_open());
	REQUIRE(!old_x86_64_runtime.is_open());

	std::ostringstream std_io_text;
	std_io_text << std_io.rdbuf();
	REQUIRE(contains(std_io_text.str(), "print(alloc_i32_to_str(value))"));
	REQUIRE(contains(std_io_text.str(), "println(alloc_i32_to_str(value))"));
	REQUIRE(!contains(std_io_text.str(), "fn print_i32_positive"));
}

TEST_CASE(stdlib_declares_all_public_functions)
{
	auto print = rexc::stdlib::find_stdlib_function("print");
	auto println = rexc::stdlib::find_stdlib_function("println");
	auto read_line = rexc::stdlib::find_stdlib_function("read_line");
	auto strlen = rexc::stdlib::find_stdlib_function("strlen");
	auto str_eq = rexc::stdlib::find_stdlib_function("str_eq");
	auto str_is_empty = rexc::stdlib::find_stdlib_function("str_is_empty");
	auto str_starts_with = rexc::stdlib::find_stdlib_function("str_starts_with");
	auto str_ends_with = rexc::stdlib::find_stdlib_function("str_ends_with");
	auto str_contains = rexc::stdlib::find_stdlib_function("str_contains");
	auto str_find = rexc::stdlib::find_stdlib_function("str_find");
	auto memset_u8 = rexc::stdlib::find_stdlib_function("memset_u8");
	auto memcpy_u8 = rexc::stdlib::find_stdlib_function("memcpy_u8");
	auto str_copy_to = rexc::stdlib::find_stdlib_function("str_copy_to");
	auto slice_u8_len = rexc::stdlib::find_stdlib_function("slice_u8_len");
	auto slice_u8_is_empty = rexc::stdlib::find_stdlib_function("slice_u8_is_empty");
	auto slice_u8_at = rexc::stdlib::find_stdlib_function("slice_u8_at");
	auto slice_u8_get_or = rexc::stdlib::find_stdlib_function("slice_u8_get_or");
	auto slice_i32_from = rexc::stdlib::find_stdlib_function("slice_i32_from");
	auto slice_i32_len = rexc::stdlib::find_stdlib_function("slice_i32_len");
	auto slice_i32_at = rexc::stdlib::find_stdlib_function("slice_i32_at");
	auto slice_i32_get_or = rexc::stdlib::find_stdlib_function("slice_i32_get_or");
	auto result_is_ok = rexc::stdlib::find_stdlib_function("result_is_ok");
	auto result_is_err = rexc::stdlib::find_stdlib_function("result_is_err");
	auto result_i32_ok = rexc::stdlib::find_stdlib_function("result_i32_ok");
	auto result_i32_err = rexc::stdlib::find_stdlib_function("result_i32_err");
	auto result_i32_is_ok = rexc::stdlib::find_stdlib_function("result_i32_is_ok");
	auto result_i32_is_err = rexc::stdlib::find_stdlib_function("result_i32_is_err");
	auto result_i32_value_or = rexc::stdlib::find_stdlib_function("result_i32_value_or");
	auto result_i32_error = rexc::stdlib::find_stdlib_function("result_i32_error");
	auto error_out_of_memory = rexc::stdlib::find_stdlib_function("error_out_of_memory");
	auto alloc_bytes = rexc::stdlib::find_stdlib_function("alloc_bytes");
	auto std_alloc_remaining_path =
		rexc::stdlib::find_stdlib_function("std::alloc::remaining");
	auto std_alloc_reset_path =
		rexc::stdlib::find_stdlib_function("std::alloc::reset");
	auto std_alloc_bytes_path =
		rexc::stdlib::find_stdlib_function("std::alloc::bytes");
	auto alloc_used = rexc::stdlib::find_stdlib_function("alloc_used");
	auto alloc_can_allocate = rexc::stdlib::find_stdlib_function("alloc_can_allocate");
	auto alloc_str_copy = rexc::stdlib::find_stdlib_function("alloc_str_copy");
	auto alloc_str_concat = rexc::stdlib::find_stdlib_function("alloc_str_concat");
	auto owned_str_clone = rexc::stdlib::find_stdlib_function("owned_str_clone");
	auto box_i32_new = rexc::stdlib::find_stdlib_function("box_i32_new");
	auto vec_i32_new = rexc::stdlib::find_stdlib_function("vec_i32_new");
	auto vec_i32_push = rexc::stdlib::find_stdlib_function("vec_i32_push");
	auto alloc_i32_to_str = rexc::stdlib::find_stdlib_function("alloc_i32_to_str");
	auto alloc_bool_to_str = rexc::stdlib::find_stdlib_function("alloc_bool_to_str");
	auto alloc_char_to_str = rexc::stdlib::find_stdlib_function("alloc_char_to_str");
	auto alloc_remaining = rexc::stdlib::find_stdlib_function("alloc_remaining");
	auto alloc_reset = rexc::stdlib::find_stdlib_function("alloc_reset");
	auto print_i32 = rexc::stdlib::find_stdlib_function("print_i32");
	auto println_i32 = rexc::stdlib::find_stdlib_function("println_i32");
	auto print_bool = rexc::stdlib::find_stdlib_function("print_bool");
	auto println_bool = rexc::stdlib::find_stdlib_function("println_bool");
	auto print_char = rexc::stdlib::find_stdlib_function("print_char");
	auto println_char = rexc::stdlib::find_stdlib_function("println_char");
	auto parse_i32 = rexc::stdlib::find_stdlib_function("parse_i32");
	auto read_i32 = rexc::stdlib::find_stdlib_function("read_i32");
	auto parse_bool = rexc::stdlib::find_stdlib_function("parse_bool");
	auto read_bool = rexc::stdlib::find_stdlib_function("read_bool");
	auto exit = rexc::stdlib::find_stdlib_function("exit");
	auto panic = rexc::stdlib::find_stdlib_function("panic");
	auto abort = rexc::stdlib::find_stdlib_function("abort");
	auto std_io_println = rexc::stdlib::find_stdlib_function("std_io_println");
	auto std_io_println_path = rexc::stdlib::find_stdlib_function("std::io::println");
	auto std_io_write_path = rexc::stdlib::find_stdlib_function("std::io::write");
	auto std_io_write_str_path =
		rexc::stdlib::find_stdlib_function("std::io::write_str");
	auto std_process_exit = rexc::stdlib::find_stdlib_function("std_process_exit");
	auto std_process_exit_path =
		rexc::stdlib::find_stdlib_function("std::process::exit");
	auto args_len = rexc::stdlib::find_stdlib_function("args_len");
	auto arg_at = rexc::stdlib::find_stdlib_function("arg_at");
	auto std_process_args_len_path =
		rexc::stdlib::find_stdlib_function("std::process::args_len");
	auto std_process_arg_at_path =
		rexc::stdlib::find_stdlib_function("std::process::arg_at");
	auto std_process_kill_path =
		rexc::stdlib::find_stdlib_function("std::process::kill");
	auto std_process_execve_path =
		rexc::stdlib::find_stdlib_function("std::process::execve");
	auto env_get = rexc::stdlib::find_stdlib_function("env_get");
	auto std_env_len_path =
		rexc::stdlib::find_stdlib_function("std::env::len");
	auto std_env_at_path =
		rexc::stdlib::find_stdlib_function("std::env::at");
	auto std_env_get_path =
		rexc::stdlib::find_stdlib_function("std::env::get");
	auto file_open_read = rexc::stdlib::find_stdlib_function("file_open_read");
	auto file_read = rexc::stdlib::find_stdlib_function("file_read");
	auto file_close = rexc::stdlib::find_stdlib_function("file_close");
	auto std_fs_open_read_path =
		rexc::stdlib::find_stdlib_function("std::fs::open_read");
	auto std_fs_read_path =
		rexc::stdlib::find_stdlib_function("std::fs::read");
	auto std_fs_close_path =
		rexc::stdlib::find_stdlib_function("std::fs::close");
	auto std_fs_getdents_path =
		rexc::stdlib::find_stdlib_function("std::fs::getdents");
	auto file_write_str = rexc::stdlib::find_stdlib_function("file_write_str");
	auto path_join = rexc::stdlib::find_stdlib_function("path_join");
	auto std_time_sleep_path =
		rexc::stdlib::find_stdlib_function("std::time::sleep");
	auto std_time_unix_seconds_path =
		rexc::stdlib::find_stdlib_function("std::time::unix_seconds");

	REQUIRE(print != nullptr);
	REQUIRE(println != nullptr);
	REQUIRE(read_line != nullptr);
	REQUIRE(strlen != nullptr);
	REQUIRE(str_eq != nullptr);
	REQUIRE(str_is_empty != nullptr);
	REQUIRE(str_starts_with != nullptr);
	REQUIRE(str_ends_with != nullptr);
	REQUIRE(str_contains != nullptr);
	REQUIRE(str_find != nullptr);
	REQUIRE(memset_u8 != nullptr);
	REQUIRE(memcpy_u8 != nullptr);
	REQUIRE(str_copy_to != nullptr);
	REQUIRE(slice_u8_len != nullptr);
	REQUIRE(slice_u8_is_empty != nullptr);
	REQUIRE(slice_u8_at != nullptr);
	REQUIRE(slice_u8_get_or != nullptr);
	REQUIRE(slice_i32_from != nullptr);
	REQUIRE(slice_i32_len != nullptr);
	REQUIRE(slice_i32_at != nullptr);
	REQUIRE(slice_i32_get_or != nullptr);
	REQUIRE(result_is_ok != nullptr);
	REQUIRE(result_is_err != nullptr);
	REQUIRE(result_i32_ok != nullptr);
	REQUIRE(result_i32_err != nullptr);
	REQUIRE(result_i32_is_ok != nullptr);
	REQUIRE(result_i32_is_err != nullptr);
	REQUIRE(result_i32_value_or != nullptr);
	REQUIRE(result_i32_error != nullptr);
	REQUIRE(error_out_of_memory != nullptr);
	REQUIRE(alloc_bytes != nullptr);
	REQUIRE(std_alloc_remaining_path != nullptr);
	REQUIRE(std_alloc_reset_path != nullptr);
	REQUIRE(std_alloc_bytes_path != nullptr);
	REQUIRE(alloc_used != nullptr);
	REQUIRE(alloc_can_allocate != nullptr);
	REQUIRE(alloc_str_copy != nullptr);
	REQUIRE(alloc_str_concat != nullptr);
	REQUIRE(owned_str_clone != nullptr);
	REQUIRE(box_i32_new != nullptr);
	REQUIRE(vec_i32_new != nullptr);
	REQUIRE(vec_i32_push != nullptr);
	REQUIRE(alloc_i32_to_str != nullptr);
	REQUIRE(alloc_bool_to_str != nullptr);
	REQUIRE(alloc_char_to_str != nullptr);
	REQUIRE(alloc_remaining != nullptr);
	REQUIRE(alloc_reset != nullptr);
	REQUIRE(print_i32 != nullptr);
	REQUIRE(println_i32 != nullptr);
	REQUIRE(print_bool != nullptr);
	REQUIRE(println_bool != nullptr);
	REQUIRE(print_char != nullptr);
	REQUIRE(println_char != nullptr);
	REQUIRE(parse_i32 != nullptr);
	REQUIRE(read_i32 != nullptr);
	REQUIRE(parse_bool != nullptr);
	REQUIRE(read_bool != nullptr);
	REQUIRE(exit != nullptr);
	REQUIRE(panic != nullptr);
	REQUIRE(abort != nullptr);
	REQUIRE(std_io_println != nullptr);
	REQUIRE(std_io_println_path != nullptr);
	REQUIRE(std_io_write_path != nullptr);
	REQUIRE(std_io_write_str_path != nullptr);
	REQUIRE(std_process_exit != nullptr);
	REQUIRE(std_process_exit_path != nullptr);
	REQUIRE(args_len != nullptr);
	REQUIRE(arg_at != nullptr);
	REQUIRE(std_process_args_len_path != nullptr);
	REQUIRE(std_process_arg_at_path != nullptr);
	REQUIRE(std_process_kill_path != nullptr);
	REQUIRE(std_process_execve_path != nullptr);
	REQUIRE(env_get != nullptr);
	REQUIRE(std_env_len_path != nullptr);
	REQUIRE(std_env_at_path != nullptr);
	REQUIRE(std_env_get_path != nullptr);
	REQUIRE(file_open_read != nullptr);
	REQUIRE(file_read != nullptr);
	REQUIRE(file_close != nullptr);
	REQUIRE(std_fs_open_read_path != nullptr);
	REQUIRE(std_fs_read_path != nullptr);
	REQUIRE(std_fs_close_path != nullptr);
	REQUIRE(std_fs_getdents_path != nullptr);
	REQUIRE(file_write_str != nullptr);
	REQUIRE(path_join != nullptr);
	REQUIRE(std_time_sleep_path != nullptr);
	REQUIRE(std_time_unix_seconds_path != nullptr);
	REQUIRE_EQ(print->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(print->parameters.size(), std::size_t(1));
	REQUIRE_EQ(print->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(print->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(println->parameters.size(), std::size_t(1));
	REQUIRE_EQ(println->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(println->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(read_line->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(read_line->parameters.size(), std::size_t(0));
	REQUIRE_EQ(read_line->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(strlen->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(strlen->parameters.size(), std::size_t(1));
	REQUIRE_EQ(strlen->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(strlen->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(str_eq->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(str_eq->parameters.size(), std::size_t(2));
	REQUIRE_EQ(str_eq->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_eq->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_eq->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(str_is_empty->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(str_is_empty->parameters.size(), std::size_t(1));
	REQUIRE_EQ(str_is_empty->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_is_empty->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(str_starts_with->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(str_starts_with->parameters.size(), std::size_t(2));
	REQUIRE_EQ(str_starts_with->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_starts_with->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_starts_with->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(str_ends_with->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(str_ends_with->parameters.size(), std::size_t(2));
	REQUIRE_EQ(str_ends_with->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_ends_with->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_ends_with->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(str_contains->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(str_contains->parameters.size(), std::size_t(2));
	REQUIRE_EQ(str_contains->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_contains->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_contains->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(str_find->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(str_find->parameters.size(), std::size_t(2));
	REQUIRE_EQ(str_find->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_find->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_find->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(memset_u8->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(memset_u8->parameters.size(), std::size_t(3));
	REQUIRE_EQ(memset_u8->parameters[0], rexc::pointer_to(rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	REQUIRE_EQ(memset_u8->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	REQUIRE_EQ(memset_u8->parameters[2], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(memset_u8->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(memcpy_u8->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(memcpy_u8->parameters.size(), std::size_t(3));
	REQUIRE_EQ(memcpy_u8->parameters[0], rexc::pointer_to(rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	REQUIRE_EQ(memcpy_u8->parameters[1], rexc::pointer_to(rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	REQUIRE_EQ(memcpy_u8->parameters[2], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(memcpy_u8->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(str_copy_to->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(str_copy_to->parameters.size(), std::size_t(3));
	REQUIRE_EQ(str_copy_to->parameters[0], rexc::pointer_to(rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	REQUIRE_EQ(str_copy_to->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_copy_to->parameters[2], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(str_copy_to->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(slice_u8_len->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(slice_u8_len->parameters.size(), std::size_t(1));
	REQUIRE_EQ(slice_u8_len->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(slice_u8_is_empty->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	auto slice_u8_type = rexc::parse_primitive_type("&[u8]");
	REQUIRE(slice_u8_type.has_value());
	REQUIRE_EQ(slice_u8_len->parameters[0], *slice_u8_type);
	REQUIRE_EQ(slice_u8_at->parameters.size(), std::size_t(2));
	REQUIRE_EQ(slice_u8_at->parameters[0], *slice_u8_type);
	REQUIRE_EQ(slice_u8_at->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	REQUIRE_EQ(slice_u8_get_or->parameters.size(), std::size_t(3));
	REQUIRE_EQ(slice_u8_get_or->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	auto owned_str_type = rexc::parse_primitive_type("owned_str");
	auto slice_i32_type = rexc::parse_primitive_type("slice<i32>");
	auto vec_i32_type = rexc::parse_primitive_type("vec<i32>");
	auto result_i32_type = rexc::parse_primitive_type("Result<i32>");
	REQUIRE(owned_str_type.has_value());
	REQUIRE(slice_i32_type.has_value());
	REQUIRE(vec_i32_type.has_value());
	REQUIRE(result_i32_type.has_value());
	REQUIRE_EQ(slice_i32_from->parameters.size(), std::size_t(2));
	REQUIRE_EQ(slice_i32_from->parameters[0], rexc::pointer_to(rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(slice_i32_from->return_type, *slice_i32_type);
	REQUIRE_EQ(slice_i32_len->parameters.size(), std::size_t(1));
	REQUIRE_EQ(slice_i32_len->parameters[0], *slice_i32_type);
	REQUIRE_EQ(slice_i32_len->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(slice_i32_at->parameters.size(), std::size_t(2));
	REQUIRE_EQ(slice_i32_at->parameters[0], *slice_i32_type);
	REQUIRE_EQ(slice_i32_at->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(slice_i32_get_or->parameters.size(), std::size_t(3));
	REQUIRE_EQ(slice_i32_get_or->parameters[0], *slice_i32_type);
	REQUIRE_EQ(slice_i32_get_or->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(result_is_ok->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(result_is_err->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(result_i32_ok->return_type, *result_i32_type);
	REQUIRE_EQ(result_i32_err->return_type, *result_i32_type);
	REQUIRE_EQ(result_i32_is_ok->parameters[0], *result_i32_type);
	REQUIRE_EQ(result_i32_is_ok->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(result_i32_is_err->parameters[0], *result_i32_type);
	REQUIRE_EQ(result_i32_is_err->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(result_i32_value_or->parameters[0], *result_i32_type);
	REQUIRE_EQ(result_i32_value_or->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(result_i32_error->parameters[0], *result_i32_type);
	REQUIRE_EQ(result_i32_error->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(error_out_of_memory->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(alloc_bytes->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_bytes->parameters.size(), std::size_t(1));
	REQUIRE_EQ(alloc_bytes->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(alloc_bytes->return_type, rexc::pointer_to(rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	REQUIRE_EQ(std_alloc_remaining_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_alloc_reset_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_alloc_bytes_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_alloc_bytes_path->return_type, rexc::pointer_to(rexc::PrimitiveType{rexc::PrimitiveKind::UnsignedInteger, 8}));
	REQUIRE_EQ(alloc_str_copy->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_str_copy->parameters.size(), std::size_t(1));
	REQUIRE_EQ(alloc_str_copy->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(alloc_str_copy->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(alloc_str_concat->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_str_concat->parameters.size(), std::size_t(2));
	REQUIRE_EQ(alloc_str_concat->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(alloc_str_concat->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(alloc_str_concat->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(alloc_i32_to_str->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_i32_to_str->parameters.size(), std::size_t(1));
	REQUIRE_EQ(alloc_i32_to_str->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(alloc_i32_to_str->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(alloc_bool_to_str->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_bool_to_str->parameters.size(), std::size_t(1));
	REQUIRE_EQ(alloc_bool_to_str->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(alloc_bool_to_str->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(alloc_char_to_str->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_char_to_str->parameters.size(), std::size_t(1));
	REQUIRE_EQ(alloc_char_to_str->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Char}));
	REQUIRE_EQ(alloc_char_to_str->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(alloc_remaining->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_remaining->parameters.size(), std::size_t(0));
	REQUIRE_EQ(alloc_remaining->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(alloc_used->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_used->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(alloc_can_allocate->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(owned_str_clone->return_type, *owned_str_type);
	REQUIRE_EQ(box_i32_new->return_type, rexc::pointer_to(rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(vec_i32_new->return_type, *vec_i32_type);
	REQUIRE_EQ(vec_i32_push->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(alloc_reset->layer, rexc::stdlib::Layer::Alloc);
	REQUIRE_EQ(alloc_reset->parameters.size(), std::size_t(0));
	REQUIRE_EQ(alloc_reset->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(print_i32->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(print_i32->parameters.size(), std::size_t(1));
	REQUIRE_EQ(print_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(print_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println_i32->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(println_i32->parameters.size(), std::size_t(1));
	REQUIRE_EQ(println_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(print_bool->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(print_bool->parameters.size(), std::size_t(1));
	REQUIRE_EQ(print_bool->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(print_bool->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println_bool->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(println_bool->parameters.size(), std::size_t(1));
	REQUIRE_EQ(println_bool->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(println_bool->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(print_char->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(print_char->parameters.size(), std::size_t(1));
	REQUIRE_EQ(print_char->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Char}));
	REQUIRE_EQ(print_char->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println_char->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(println_char->parameters.size(), std::size_t(1));
	REQUIRE_EQ(println_char->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Char}));
	REQUIRE_EQ(println_char->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(parse_i32->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(parse_i32->parameters.size(), std::size_t(1));
	REQUIRE_EQ(parse_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(parse_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(read_i32->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(read_i32->parameters.size(), std::size_t(0));
	REQUIRE_EQ(read_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(parse_bool->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(parse_bool->parameters.size(), std::size_t(1));
	REQUIRE_EQ(parse_bool->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(parse_bool->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(read_bool->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(read_bool->parameters.size(), std::size_t(0));
	REQUIRE_EQ(read_bool->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(exit->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(exit->parameters.size(), std::size_t(1));
	REQUIRE_EQ(exit->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(exit->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(panic->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(panic->parameters.size(), std::size_t(1));
	REQUIRE_EQ(panic->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(panic->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(abort->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_io_println->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_io_println_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_io_write_path->parameters.size(), std::size_t(3));
	REQUIRE_EQ(std_io_write_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_io_write_str_path->parameters.size(), std::size_t(2));
	REQUIRE_EQ(std_io_write_str_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_process_exit->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_process_exit_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(args_len->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(arg_at->parameters.size(), std::size_t(1));
	REQUIRE_EQ(arg_at->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(std_process_args_len_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_process_arg_at_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_process_arg_at_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(std_process_kill_path->parameters.size(), std::size_t(2));
	REQUIRE_EQ(std_process_kill_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_process_execve_path->parameters.size(), std::size_t(3));
	REQUIRE_EQ(std_process_execve_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(env_get->parameters.size(), std::size_t(1));
	REQUIRE_EQ(env_get->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(std_env_len_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_env_at_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_env_at_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(std_env_get_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_env_get_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(file_open_read->parameters.size(), std::size_t(1));
	REQUIRE_EQ(file_open_read->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(file_read->parameters.size(), std::size_t(3));
	REQUIRE_EQ(file_read->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(file_close->parameters.size(), std::size_t(1));
	REQUIRE_EQ(file_close->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_fs_open_read_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_fs_open_read_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_fs_read_path->parameters.size(), std::size_t(3));
	REQUIRE_EQ(std_fs_read_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_fs_close_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_fs_close_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_fs_getdents_path->parameters.size(), std::size_t(3));
	REQUIRE_EQ(std_fs_getdents_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(file_write_str->parameters.size(), std::size_t(2));
	REQUIRE_EQ(path_join->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(std_time_sleep_path->parameters.size(), std::size_t(1));
	REQUIRE_EQ(std_time_sleep_path->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_time_sleep_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(std_time_unix_seconds_path->parameters.size(), std::size_t(0));
	REQUIRE_EQ(std_time_unix_seconds_path->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
}

TEST_CASE(stdlib_default_prelude_contains_only_user_facing_names)
{
	auto println = rexc::stdlib::find_prelude_function("println");
	auto read_line = rexc::stdlib::find_prelude_function("read_line");
	auto str_eq = rexc::stdlib::find_prelude_function("str_eq");
	auto parse_i32 = rexc::stdlib::find_prelude_function("parse_i32");
	auto print_i32 = rexc::stdlib::find_prelude_function("print_i32");
	auto panic = rexc::stdlib::find_prelude_function("panic");

	REQUIRE(println != nullptr);
	REQUIRE(read_line != nullptr);
	REQUIRE(str_eq != nullptr);
	REQUIRE(parse_i32 != nullptr);
	REQUIRE(print_i32 != nullptr);
	REQUIRE(panic != nullptr);

	REQUIRE(rexc::stdlib::find_prelude_function("memset_u8") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("alloc_bytes") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("alloc_i32_to_str") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("result_is_ok") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("file_open_read") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("path_join") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("std_io_println") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("std::io::println") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("std::process::exit") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("exit") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("abort") == nullptr);
}

TEST_CASE(stdlib_emits_hosted_runtime_symbols)
{
	auto i386 = rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::I386Linux);
	auto i386_drunix =
		rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::I386Drunix);
	auto x86_64 =
		rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::X86_64Linux);
	auto arm64 = rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::ARM64Macos);

	REQUIRE(contains(i386, "print:"));
	REQUIRE(contains(i386, "println:"));
	REQUIRE(contains(i386, "read_line:"));
	REQUIRE(contains(i386, "strlen:"));
	REQUIRE(contains(i386, "str_eq:"));
	REQUIRE(contains(i386, "str_is_empty:"));
	REQUIRE(contains(i386, "str_starts_with:"));
	REQUIRE(contains(i386, "str_ends_with:"));
	REQUIRE(contains(i386, "str_contains:"));
	REQUIRE(contains(i386, "str_find:"));
	REQUIRE(contains(i386, "memset_u8:"));
	REQUIRE(contains(i386, "memcpy_u8:"));
	REQUIRE(contains(i386, "str_copy_to:"));
	REQUIRE(contains(i386, "alloc_bytes:"));
	REQUIRE(contains(i386, "alloc_str_copy:"));
	REQUIRE(contains(i386, "alloc_str_concat:"));
	REQUIRE(contains(i386, "alloc_i32_to_str:"));
	REQUIRE(contains(i386, "alloc_bool_to_str:"));
	REQUIRE(contains(i386, "alloc_char_to_str:"));
	REQUIRE(contains(i386, "alloc_remaining:"));
	REQUIRE(contains(i386, "alloc_reset:"));
	REQUIRE(contains(i386, "print_i32:"));
	REQUIRE(contains(i386, "println_i32:"));
	REQUIRE(contains(i386, "print_bool:"));
	REQUIRE(contains(i386, "println_bool:"));
	REQUIRE(contains(i386, "print_char:"));
	REQUIRE(contains(i386, "println_char:"));
	REQUIRE(contains(i386, "parse_i32:"));
	REQUIRE(contains(i386, "read_i32:"));
	REQUIRE(contains(i386, "parse_bool:"));
	REQUIRE(contains(i386, "read_bool:"));
	REQUIRE(contains(i386, "exit:"));
	REQUIRE(contains(i386, "panic:"));
	REQUIRE(contains(i386, "sys_write:"));
	REQUIRE(contains(i386, "sys_read:"));
	REQUIRE(contains(i386, "sys_exit:"));
	REQUIRE(contains(i386, "sys_sleep:"));
	REQUIRE(contains(i386, "sys_unix_seconds:"));
	REQUIRE(contains(i386, "sys_args_len:"));
	REQUIRE(contains(i386, "sys_arg:"));
	REQUIRE(contains(i386, "sys_env_len:"));
	REQUIRE(contains(i386, "sys_env_at:"));
	REQUIRE(contains(i386, "sys_kill:"));
	REQUIRE(contains(i386, "sys_execve:"));
	REQUIRE(contains(i386, "sys_trap_ud2:"));
	REQUIRE(contains(i386, "sys_trap_gpfault:"));
	REQUIRE(contains(i386, "call sys_write"));
	REQUIRE(contains(i386, "call sys_read"));
	REQUIRE(contains(i386, "call sys_exit"));
	REQUIRE(contains(i386, "call sys_sleep"));
	REQUIRE(contains(i386, "call sys_unix_seconds"));
	REQUIRE(!contains(i386, "sys_read_line:"));
	REQUIRE(contains(i386, "int $0x80"));

	REQUIRE(contains(i386_drunix, "print:"));
	REQUIRE(contains(i386_drunix, "println:"));
	REQUIRE(contains(i386_drunix, "read_line:"));
	REQUIRE(contains(i386_drunix, "sys_write:"));
	REQUIRE(contains(i386_drunix, "sys_read:"));
	REQUIRE(contains(i386_drunix, "sys_exit:"));
	REQUIRE(contains(i386_drunix, "sys_sleep:"));
	REQUIRE(contains(i386_drunix, "sys_unix_seconds:"));
	REQUIRE(contains(i386_drunix, "sys_file_open_read:"));
	REQUIRE(contains(i386_drunix, "sys_file_create_write:"));
	REQUIRE(contains(i386_drunix, "sys_file_close:"));
	REQUIRE(contains(i386_drunix, "sys_getdents:"));
	REQUIRE(contains(i386_drunix, "sys_args_len:"));
	REQUIRE(contains(i386_drunix, "sys_arg:"));
	REQUIRE(contains(i386_drunix, "sys_env_len:"));
	REQUIRE(contains(i386_drunix, "sys_env_at:"));
	REQUIRE(contains(i386_drunix, "sys_kill:"));
	REQUIRE(contains(i386_drunix, "sys_execve:"));
	REQUIRE(contains(i386_drunix, "sys_trap_ud2:"));
	REQUIRE(contains(i386_drunix, "sys_trap_gpfault:"));
	REQUIRE(contains(i386_drunix, "movl $162, %eax"));
	REQUIRE(contains(i386_drunix, "movl $265, %eax"));
	REQUIRE(contains(i386_drunix, ".globl __rexc_argc"));
	REQUIRE(contains(i386_drunix, ".globl __rexc_argv"));
	REQUIRE(contains(i386_drunix, ".globl __rexc_envp"));
	REQUIRE(contains(i386_drunix, "movl __rexc_argc, %eax"));
	REQUIRE(contains(i386_drunix, "movl __rexc_argv, %eax"));
	REQUIRE(contains(i386_drunix, "movl __rexc_envp, %ecx"));
	REQUIRE(contains(i386_drunix, "movl $8, %eax"));
	REQUIRE(contains(i386_drunix, "int $0x80"));

	REQUIRE(contains(x86_64, "print:"));
	REQUIRE(contains(x86_64, "println:"));
	REQUIRE(contains(x86_64, "read_line:"));
	REQUIRE(contains(x86_64, "strlen:"));
	REQUIRE(contains(x86_64, "str_eq:"));
	REQUIRE(contains(x86_64, "str_is_empty:"));
	REQUIRE(contains(x86_64, "str_starts_with:"));
	REQUIRE(contains(x86_64, "str_ends_with:"));
	REQUIRE(contains(x86_64, "str_contains:"));
	REQUIRE(contains(x86_64, "str_find:"));
	REQUIRE(contains(x86_64, "memset_u8:"));
	REQUIRE(contains(x86_64, "memcpy_u8:"));
	REQUIRE(contains(x86_64, "str_copy_to:"));
	REQUIRE(contains(x86_64, "alloc_bytes:"));
	REQUIRE(contains(x86_64, "alloc_str_copy:"));
	REQUIRE(contains(x86_64, "alloc_str_concat:"));
	REQUIRE(contains(x86_64, "alloc_i32_to_str:"));
	REQUIRE(contains(x86_64, "alloc_bool_to_str:"));
	REQUIRE(contains(x86_64, "alloc_char_to_str:"));
	REQUIRE(contains(x86_64, "alloc_remaining:"));
	REQUIRE(contains(x86_64, "alloc_reset:"));
	REQUIRE(contains(x86_64, "print_i32:"));
	REQUIRE(contains(x86_64, "println_i32:"));
	REQUIRE(contains(x86_64, "print_bool:"));
	REQUIRE(contains(x86_64, "println_bool:"));
	REQUIRE(contains(x86_64, "print_char:"));
	REQUIRE(contains(x86_64, "println_char:"));
	REQUIRE(contains(x86_64, "parse_i32:"));
	REQUIRE(contains(x86_64, "read_i32:"));
	REQUIRE(contains(x86_64, "parse_bool:"));
	REQUIRE(contains(x86_64, "read_bool:"));
	REQUIRE(contains(x86_64, "exit:"));
	REQUIRE(contains(x86_64, "panic:"));
	REQUIRE(contains(x86_64, "sys_write:"));
	REQUIRE(contains(x86_64, "sys_read:"));
	REQUIRE(contains(x86_64, "sys_exit:"));
	REQUIRE(contains(x86_64, "sys_sleep:"));
	REQUIRE(contains(x86_64, "sys_unix_seconds:"));
	REQUIRE(contains(x86_64, "sys_args_len:"));
	REQUIRE(contains(x86_64, "sys_arg:"));
	REQUIRE(contains(x86_64, "sys_env_len:"));
	REQUIRE(contains(x86_64, "sys_env_at:"));
	REQUIRE(contains(x86_64, "sys_kill:"));
	REQUIRE(contains(x86_64, "sys_getdents:"));
	REQUIRE(contains(x86_64, "sys_execve:"));
	REQUIRE(contains(x86_64, "sys_trap_ud2:"));
	REQUIRE(contains(x86_64, "sys_trap_gpfault:"));
	REQUIRE(contains(x86_64, "call sys_write"));
	REQUIRE(contains(x86_64, "call sys_read"));
	REQUIRE(contains(x86_64, "call sys_exit"));
	REQUIRE(contains(x86_64, "call sys_sleep"));
	REQUIRE(contains(x86_64, "call sys_unix_seconds"));
	REQUIRE(!contains(x86_64, "sys_read_line:"));
	REQUIRE(contains(x86_64, "syscall"));
	REQUIRE(!contains(x86_64, "movq %rdi, %rdi"));

	REQUIRE(contains(arm64, "_print:"));
	REQUIRE(contains(arm64, "_println:"));
	REQUIRE(contains(arm64, "_read_line:"));
	REQUIRE(contains(arm64, "_strlen:"));
	REQUIRE(contains(arm64, "_str_eq:"));
	REQUIRE(contains(arm64, "_str_is_empty:"));
	REQUIRE(contains(arm64, "_str_starts_with:"));
	REQUIRE(contains(arm64, "_str_ends_with:"));
	REQUIRE(contains(arm64, "_str_contains:"));
	REQUIRE(contains(arm64, "_str_find:"));
	REQUIRE(contains(arm64, "_memset_u8:"));
	REQUIRE(contains(arm64, "_memcpy_u8:"));
	REQUIRE(contains(arm64, "_str_copy_to:"));
	REQUIRE(contains(arm64, "_alloc_bytes:"));
	REQUIRE(contains(arm64, "_alloc_str_copy:"));
	REQUIRE(contains(arm64, "_alloc_str_concat:"));
	REQUIRE(contains(arm64, "_alloc_i32_to_str:"));
	REQUIRE(contains(arm64, "_alloc_bool_to_str:"));
	REQUIRE(contains(arm64, "_alloc_char_to_str:"));
	REQUIRE(contains(arm64, "_alloc_remaining:"));
	REQUIRE(contains(arm64, "_alloc_reset:"));
	REQUIRE(contains(arm64, "_print_i32:"));
	REQUIRE(contains(arm64, "_println_i32:"));
	REQUIRE(contains(arm64, "_print_bool:"));
	REQUIRE(contains(arm64, "_println_bool:"));
	REQUIRE(contains(arm64, "_print_char:"));
	REQUIRE(contains(arm64, "_println_char:"));
	REQUIRE(contains(arm64, "_parse_i32:"));
	REQUIRE(contains(arm64, "_read_i32:"));
	REQUIRE(contains(arm64, "_parse_bool:"));
	REQUIRE(contains(arm64, "_read_bool:"));
	REQUIRE(contains(arm64, "_panic:"));
	REQUIRE(contains(arm64, "_sys_write:"));
	REQUIRE(contains(arm64, "_sys_read:"));
	REQUIRE(contains(arm64, "_sys_exit:"));
	REQUIRE(contains(arm64, "_sys_sleep:"));
	REQUIRE(contains(arm64, "_sys_unix_seconds:"));
	REQUIRE(!contains(arm64, ".globl _sleep\n_sleep:"));
	REQUIRE(contains(arm64, "_sys_args_len:"));
	REQUIRE(contains(arm64, "_sys_arg:"));
	REQUIRE(contains(arm64, "_sys_env_len:"));
	REQUIRE(contains(arm64, "_sys_env_at:"));
	REQUIRE(contains(arm64, "_sys_kill:"));
	REQUIRE(contains(arm64, "_sys_getdents:"));
	REQUIRE(contains(arm64, "_sys_execve:"));
	REQUIRE(contains(arm64, "_sys_trap_ud2:"));
	REQUIRE(contains(arm64, "_sys_trap_gpfault:"));
	REQUIRE(contains(arm64, "bl _sys_write"));
	REQUIRE(contains(arm64, "bl _sys_read"));
	REQUIRE(contains(arm64, "bl _sys_exit"));
	REQUIRE(contains(arm64, "bl _sys_sleep"));
	REQUIRE(contains(arm64, "bl _sys_unix_seconds"));
	REQUIRE(!contains(arm64, "_sys_read_line:"));
	REQUIRE(!contains(arm64, ".globl _write\n_write:"));
	REQUIRE(contains(arm64, "bl _write"));
	REQUIRE(contains(arm64, "bl _read"));
	REQUIRE(contains(arm64, "bl _nanosleep"));
	REQUIRE(contains(arm64, "bl _time"));
	REQUIRE(!contains(arm64, "\tbl _exit\n"));
}

TEST_CASE(stdlib_runtime_dispatch_returns_different_target_assemblies)
{
	auto i386 = rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::I386Linux);
	auto i386_drunix =
		rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::I386Drunix);
	auto x86_64 =
		rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::X86_64Linux);
	auto arm64 = rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::ARM64Macos);

	REQUIRE(i386.find("int $0x80") != std::string::npos);
	REQUIRE(i386_drunix.find("int $0x80") != std::string::npos);
	REQUIRE(i386_drunix.find(".globl __rexc_envp") != std::string::npos);
	REQUIRE(x86_64.find("syscall") != std::string::npos);
	REQUIRE(arm64.find("bl _write") != std::string::npos);
	REQUIRE(i386 != i386_drunix);
	REQUIRE(i386 != x86_64);
	REQUIRE(x86_64 != arm64);
}

// FE-105: Arena struct + arena_* explicit-allocator API are visible in
// alloc.rx and compile into the portable stdlib assembly. The
// `find_stdlib_function` lookup surface is currently keyed off
// PrimitiveType-resolvable signatures (see resolve_source_type in
// src/stdlib/stdlib.cpp), which doesn't yet recognize user-struct names
// like *Arena, so the registry skips the arena_* declarations. This
// test pins both the textual presence in alloc.rx and the codegen
// presence in the emitted runtime assembly. A follow-up (FE-106 or
// later) is expected to extend stdlib registration so user-struct
// signatures show through find_stdlib_function and a future
// find_stdlib_struct.
TEST_CASE(stdlib_exposes_arena_struct_and_arena_alloc_function)
{
	const std::string source_dir = REXC_SOURCE_DIR;
	std::ifstream alloc_file(source_dir + "/src/stdlib/alloc/alloc.rx");
	REQUIRE(alloc_file.is_open());
	std::ostringstream alloc_text;
	alloc_text << alloc_file.rdbuf();
	const std::string alloc_source = alloc_text.str();

	REQUIRE(contains(alloc_source, "struct Arena {"));
	REQUIRE(contains(alloc_source, "fn arena_init(arena: *Arena"));
	REQUIRE(contains(alloc_source, "fn arena_alloc(arena: *Arena"));
	REQUIRE(contains(alloc_source, "fn arena_remaining(arena: *Arena)"));
	REQUIRE(contains(alloc_source, "fn arena_used(arena: *Arena)"));
	REQUIRE(contains(alloc_source, "fn arena_capacity(arena: *Arena)"));
	REQUIRE(contains(alloc_source, "fn arena_can_allocate(arena: *Arena"));
	REQUIRE(contains(alloc_source, "fn arena_reset(arena: *Arena)"));

	auto arm64 = rexc::stdlib::hosted_runtime_assembly(rexc::TargetTriple::ARM64Macos);
	REQUIRE(contains(arm64, "_arena_init:"));
	REQUIRE(contains(arm64, "_arena_alloc:"));
	REQUIRE(contains(arm64, "_arena_remaining:"));
	REQUIRE(contains(arm64, "_arena_used:"));
	REQUIRE(contains(arm64, "_arena_capacity:"));
	REQUIRE(contains(arm64, "_arena_can_allocate:"));
	REQUIRE(contains(arm64, "_arena_reset:"));

	// Document the current registration gap: arena_* takes/returns
	// *Arena, which parse_primitive_type does not yet recognize, so
	// these symbols are intentionally absent from find_stdlib_function
	// today even though the names are reserved through
	// reserved_runtime_symbols and the bodies are compiled into the
	// stdlib assembly above.
	auto arena_alloc = rexc::stdlib::find_stdlib_function("arena_alloc");
	REQUIRE(arena_alloc == nullptr);
}
