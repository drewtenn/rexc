// rexc command-line driver.
//
// main.cpp owns process-level concerns: command-line parsing, file I/O, stage
// sequencing, diagnostic printing, and exit status. The actual compiler work is
// delegated to parse, sema, IR lowering, and x86 codegen so the CLI remains a
// thin coordinator from .rx source text to emitted assembly.
#include "rexc/codegen_arm64.hpp"
#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/stdlib.hpp"
#include "rexc/target.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

enum class OutputMode {
	Assembly,
	Object,
	Executable,
	DrunixExecutable,
};

enum class DiagnosticMode {
	Human,
	Json,
};

struct Options {
	std::string input_path;
	std::string output_path;
	std::string drunix_root;
	std::vector<std::string> package_paths;
	rexc::TargetTriple target =
#if defined(__APPLE__)
		rexc::TargetTriple::ARM64Macos;
#else
		rexc::TargetTriple::I386Linux;
#endif
	OutputMode output_mode = OutputMode::Assembly;
	bool output_mode_selected = false;
	DiagnosticMode diagnostic_mode = DiagnosticMode::Human;
};

void select_output_mode(Options &options, OutputMode mode)
{
	if (options.output_mode_selected && options.output_mode != mode)
		throw std::runtime_error("usage");
	options.output_mode = mode;
	options.output_mode_selected = true;
}

void require_directory(const std::string &path, const std::string &description)
{
	std::error_code error;
	if (std::filesystem::is_directory(path, error))
		return;

	std::string message = description + " is not a directory: " + path;
	if (error)
		message += " (" + error.message() + ")";
	throw std::runtime_error(message);
}

Options parse_options(int argc, char **argv)
{
	Options options;
	if (argc < 4)
		throw std::runtime_error("usage");

	options.input_path = argv[1];
	for (int i = 2; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-S") {
			select_output_mode(options, OutputMode::Assembly);
			continue;
		}
		if (arg == "-c") {
			select_output_mode(options, OutputMode::Object);
			continue;
		}
		if (arg == "-o" && i + 1 < argc) {
			options.output_path = argv[++i];
			continue;
		}
		if (arg == "--package-path" && i + 1 < argc) {
			std::string package_path = argv[++i];
			require_directory(package_path, "package path");
			options.package_paths.push_back(std::move(package_path));
			continue;
		}
		if (arg == "--target" && i + 1 < argc) {
			std::string target_name = argv[++i];
			auto parsed_target = rexc::parse_target_triple(target_name);
			if (!parsed_target)
				throw std::runtime_error("unknown target: " + target_name);
			options.target = *parsed_target;
			continue;
		}
		if (arg == "--diag=json") {
			options.diagnostic_mode = DiagnosticMode::Json;
			continue;
		}
		if (arg == "--drunix-root" && i + 1 < argc) {
			options.drunix_root = argv[++i];
			select_output_mode(options, OutputMode::DrunixExecutable);
			continue;
		}
		throw std::runtime_error("usage");
	}

	if (options.input_path.empty() || options.output_path.empty())
		throw std::runtime_error("usage");

	if (!options.output_mode_selected) {
		options.output_mode = OutputMode::Executable;
		options.output_mode_selected = true;
	}

	if (options.output_mode == OutputMode::DrunixExecutable &&
	    rexc::codegen_target(options.target) == rexc::CodegenTarget::I386)
		options.target = rexc::TargetTriple::I386Drunix;

	return options;
}

void write_file(const std::string &path, const std::string &text)
{
	std::ofstream output(path);
	if (!output)
		throw std::runtime_error("failed to open output file: " + path);

	output << text;
}

std::string shell_quote(const std::string &value)
{
	std::string quoted = "'";
	for (char ch : value) {
		if (ch == '\'')
			quoted += "'\\''";
		else
			quoted += ch;
	}
	quoted += "'";
	return quoted;
}

bool command_exists(const std::string &name)
{
	return std::system(("command -v " + name + " >/dev/null 2>&1").c_str()) == 0;
}

bool command_succeeds(const std::string &command)
{
	return std::system((command + " >/dev/null 2>&1").c_str()) == 0;
}

std::string find_tool(const std::string &primary, const std::string &fallback,
                      const std::string &description)
{
	if (command_exists(primary))
		return primary;
	if (command_exists(fallback))
		return fallback;
	throw std::runtime_error("no " + description + " found");
}

void run_tool(const std::string &command, const std::string &failure)
{
	if (std::system(command.c_str()) != 0)
		throw std::runtime_error(failure);
}

void require_file(const std::string &path, const std::string &description)
{
	std::ifstream input(path);
	if (!input)
		throw std::runtime_error("missing " + description + ": " + path);
}

void write_cross_elf_startup(const std::string &assembly_path,
                             rexc::CodegenTarget target);

void assemble_object(const std::string &assembly_path, const std::string &object_path,
                     rexc::TargetTriple target)
{
	auto machine = rexc::codegen_target(target);
	if (machine == rexc::CodegenTarget::ARM64_MACOS) {
		if (!command_exists("as"))
			throw std::runtime_error("no Apple assembler found");
		std::string command = "as -arch arm64 -o " + shell_quote(object_path) +
		                      " " + shell_quote(assembly_path);
		run_tool(command, "assembler failed");
		return;
	}

	if (machine == rexc::CodegenTarget::ARM64_DRUNIX) {
		// arm64-drunix produces ELF; use a cross GNU assembler if one
		// is on PATH. `aarch64-elf-as` is the typical name on Apple
		// Silicon hosts (via `brew install aarch64-elf-gcc`); on Linux
		// hosts the same assembler ships as `aarch64-linux-gnu-as` or
		// the system `as` when running natively on AArch64.
		std::string assembler;
		if (command_exists("aarch64-elf-as"))
			assembler = "aarch64-elf-as";
		else if (command_exists("aarch64-linux-gnu-as"))
			assembler = "aarch64-linux-gnu-as";
		else if (command_succeeds("uname -m | grep -q aarch64") &&
		         command_succeeds("as --version 2>/dev/null | grep -qi 'gnu assembler'"))
			assembler = "as";
		else
			throw std::runtime_error(
			    "no aarch64 GNU assembler found "
			    "(install aarch64-elf-binutils or aarch64-linux-gnu-binutils)");
		std::string command = assembler + " -o " + shell_quote(object_path) +
		                      " " + shell_quote(assembly_path);
		run_tool(command, "assembler failed");
		return;
	}

	std::string assembler;
	if (command_exists("x86_64-elf-as")) {
		assembler = "x86_64-elf-as";
	} else if (command_exists("as") &&
	           command_succeeds("as --version 2>/dev/null | grep -qi 'gnu assembler'")) {
		assembler = "as";
	} else {
		throw std::runtime_error("no GNU assembler found");
	}

	std::string mode = machine == rexc::CodegenTarget::I386 ? "--32" : "--64";
	std::string command = assembler + " " + mode + " -o " +
	                      shell_quote(object_path) + " " +
	                      shell_quote(assembly_path);
	run_tool(command, "assembler failed");
}

std::string write_and_assemble_hosted_runtime(const std::string &output_path,
                                              rexc::TargetTriple target)
{
	std::string runtime_assembly = rexc::stdlib::hosted_runtime_assembly(target);
	if (runtime_assembly.empty())
		return "";

	std::string runtime_assembly_path = output_path + ".stdlib.s.tmp";
	std::string runtime_object_path = output_path + ".stdlib.o.tmp";
	write_file(runtime_assembly_path, runtime_assembly);
	try {
		assemble_object(runtime_assembly_path, runtime_object_path, target);
	} catch (...) {
		std::remove(runtime_assembly_path.c_str());
		std::remove(runtime_object_path.c_str());
		throw;
	}
	std::remove(runtime_assembly_path.c_str());
	return runtime_object_path;
}

void link_drunix_object(const std::string &object_path, const std::string &output_path,
                        const std::string &runtime_object_path,
                        const std::string &drunix_root, rexc::TargetTriple target)
{
	if (!rexc::is_drunix_target(target))
		throw std::runtime_error(
		    "internal error: link_drunix_object called with non-Drunix target");

	bool is_arm64 = target == rexc::TargetTriple::ARM64Drunix;
	std::string arch_dir = is_arm64 ? "arm64" : "x86";
	std::string linker_script =
	    drunix_root + "/build/user/" + arch_dir + "/linker/user.ld";
	require_file(linker_script, "Drunix linker script");

	std::string linker;
	std::string linker_mode;
	rexc::CodegenTarget startup_machine;
	if (is_arm64) {
		// Cross linkers for AArch64: prefer aarch64-elf-ld, fall back
		// to aarch64-linux-gnu-ld, then host `ld` if running on
		// aarch64. Same install-path advice as the assembler above.
		if (command_exists("aarch64-elf-ld"))
			linker = "aarch64-elf-ld";
		else if (command_exists("aarch64-linux-gnu-ld"))
			linker = "aarch64-linux-gnu-ld";
		else if (command_succeeds("uname -m | grep -q aarch64") &&
		         command_exists("ld"))
			linker = "ld";
		else
			throw std::runtime_error(
			    "no aarch64 ELF linker found "
			    "(install aarch64-elf-binutils or aarch64-linux-gnu-binutils)");
		linker_mode = "aarch64elf";
		startup_machine = rexc::CodegenTarget::ARM64_DRUNIX;
	} else {
		linker = find_tool("x86_64-elf-ld", "ld", "ELF linker");
		linker_mode = "elf_i386";
		startup_machine = rexc::CodegenTarget::I386;
	}

	std::string startup_assembly = output_path + ".crt0.s.tmp";
	std::string startup_object = output_path + ".crt0.o.tmp";
	write_cross_elf_startup(startup_assembly, startup_machine);
	try {
		assemble_object(startup_assembly, startup_object, target);
		std::string command = linker + " -m " + linker_mode + " -T " +
		                      shell_quote(linker_script) + " -o " +
		                      shell_quote(output_path) + " " +
		                      shell_quote(startup_object) + " " +
		                      shell_quote(object_path);
		if (!runtime_object_path.empty())
			command += " " + shell_quote(runtime_object_path);
		run_tool(command, "Drunix link failed");
	} catch (...) {
		std::remove(startup_assembly.c_str());
		std::remove(startup_object.c_str());
		std::remove(output_path.c_str());
		throw;
	}
	std::remove(startup_assembly.c_str());
	std::remove(startup_object.c_str());
}

void link_darwin_arm64_object(const std::string &object_path,
                              const std::string &runtime_object_path,
                              const std::string &output_path,
                              rexc::TargetTriple target)
{
	if (!rexc::is_darwin_target(target))
		throw std::runtime_error("Darwin linking currently supports only the arm64-macos target");
	if (!command_exists("clang"))
		throw std::runtime_error("no clang linker driver found");

	std::string command = "clang -arch arm64 " + shell_quote(object_path);
	if (!runtime_object_path.empty())
		command += " " + shell_quote(runtime_object_path);
	command += " -o " + shell_quote(output_path);
	run_tool(command, "Darwin link failed");
}

void write_cross_elf_startup(const std::string &assembly_path, rexc::CodegenTarget target)
{
	if (target == rexc::CodegenTarget::I386) {
		write_file(assembly_path,
		           ".globl _start\n"
		           "_start:\n"
		           "\tmovl (%esp), %eax\n"
		           "\tmovl %eax, __rexc_argc\n"
		           "\tleal 4(%esp), %ecx\n"
		           "\tmovl %ecx, __rexc_argv\n"
		           "\tleal 4(%ecx,%eax,4), %edx\n"
		           "\tmovl %edx, __rexc_envp\n"
		           "\tcall main\n"
		           "\tmovl %eax, %ebx\n"
		           "\tmovl $1, %eax\n"
		           "\tint $0x80\n");
		return;
	}

	if (target == rexc::CodegenTarget::ARM64_DRUNIX) {
		// AArch64 ELF entry: the kernel hands us the auxiliary vector
		// shape on the initial stack — argc at sp+0, argv at sp+8,
		// envp begins at sp + 8 + argc*8 + 8 (NULL terminator slot for
		// argv). After populating __rexc_argc/argv/envp, branch into
		// `main` and trap into `exit(rc)` via syscall #93.
		write_file(assembly_path,
		           ".globl _start\n"
		           "_start:\n"
		           "\tldr x0, [sp]\n"
		           "\tadrp x1, __rexc_argc\n"
		           "\tadd x1, x1, :lo12:__rexc_argc\n"
		           "\tstr x0, [x1]\n"
		           "\tadd x2, sp, #8\n"
		           "\tadrp x1, __rexc_argv\n"
		           "\tadd x1, x1, :lo12:__rexc_argv\n"
		           "\tstr x2, [x1]\n"
		           "\tadd x2, x2, x0, lsl #3\n"
		           "\tadd x2, x2, #8\n"
		           "\tadrp x1, __rexc_envp\n"
		           "\tadd x1, x1, :lo12:__rexc_envp\n"
		           "\tstr x2, [x1]\n"
		           "\tbl main\n"
		           "\tmov x8, #93\n"
		           "\tsvc #0\n");
		return;
	}

	write_file(assembly_path,
	           ".globl _start\n"
	           "_start:\n"
	           "\tmovq (%rsp), %rdi\n"
	           "\tleaq 8(%rsp), %rsi\n"
	           "\tmovq %rdi, __rexc_argc(%rip)\n"
	           "\tmovq %rsi, __rexc_argv(%rip)\n"
	           "\tleaq 8(%rsi,%rdi,8), %rax\n"
	           "\tmovq %rax, __rexc_envp(%rip)\n"
	           "\tcall main\n"
	           "\tmovl %eax, %edi\n"
	           "\tmovq $60, %rax\n"
	           "\tsyscall\n");
}

void link_cross_elf_object(const std::string &object_path,
                           const std::string &runtime_object_path,
                           const std::string &output_path,
                           rexc::TargetTriple target)
{
	if (rexc::is_darwin_target(target))
		throw std::runtime_error("internal error: ARM64 object reached ELF linker");
	if (rexc::is_drunix_target(target))
		throw std::runtime_error("internal error: Drunix object reached ELF linker");
	if (!command_exists("x86_64-elf-as") || !command_exists("x86_64-elf-ld"))
		throw std::runtime_error("no x86_64-elf-as/x86_64-elf-ld toolchain found");

	std::string startup_assembly = output_path + ".crt0.s.tmp";
	std::string startup_object = output_path + ".crt0.o.tmp";
	auto machine = rexc::codegen_target(target);
	write_cross_elf_startup(startup_assembly, machine);
	try {
		assemble_object(startup_assembly, startup_object, target);
		std::string linker_mode = machine == rexc::CodegenTarget::I386 ? "elf_i386" : "elf_x86_64";
		std::string command = "x86_64-elf-ld -m " + linker_mode + " -o " +
		                      shell_quote(output_path) + " " + shell_quote(startup_object) +
		                      " " + shell_quote(object_path);
		if (!runtime_object_path.empty())
			command += " " + shell_quote(runtime_object_path);
		run_tool(command, "cross ELF link failed");
	} catch (...) {
		std::remove(startup_assembly.c_str());
		std::remove(startup_object.c_str());
		std::remove(output_path.c_str());
		throw;
	}
	std::remove(startup_assembly.c_str());
	std::remove(startup_object.c_str());
}

void link_host_x86_object(const std::string &object_path,
                          const std::string &runtime_object_path,
                          const std::string &output_path,
                          rexc::TargetTriple target)
{
	if (rexc::is_darwin_target(target))
		throw std::runtime_error("internal error: ARM64 object reached x86 linker");
	if (rexc::is_drunix_target(target))
		throw std::runtime_error("internal error: Drunix object reached x86 linker");
	if (command_succeeds("uname -s | grep -q Darwin")) {
		link_cross_elf_object(object_path, runtime_object_path, output_path, target);
		return;
	}

	std::string linker = find_tool("clang", "cc", "C linker driver");
	std::string mode =
		rexc::codegen_target(target) == rexc::CodegenTarget::I386 ? "-m32" : "-m64";
	std::string command = linker + " " + mode + " " + shell_quote(object_path);
	if (!runtime_object_path.empty())
		command += " " + shell_quote(runtime_object_path);
	command += " -o " + shell_quote(output_path);
	run_tool(command, "host executable link failed");
}

void link_executable_object(const std::string &object_path,
                            const std::string &runtime_object_path,
                            const std::string &output_path,
                            rexc::TargetTriple target)
{
	if (rexc::is_drunix_target(target))
		throw std::runtime_error(
		    std::string(rexc::target_triple_name(target)) +
		    " executable links require --drunix-root");
	if (rexc::is_darwin_target(target)) {
		link_darwin_arm64_object(object_path, runtime_object_path, output_path,
		                         target);
		return;
	}
	link_host_x86_object(object_path, runtime_object_path, output_path, target);
}

void remove_if_present(const std::string &path)
{
	std::remove(path.c_str());
}

std::string format_diagnostics(const rexc::Diagnostics &diagnostics,
                               DiagnosticMode mode)
{
	if (mode == DiagnosticMode::Json)
		return diagnostics.format_json();
	return diagnostics.format();
}

} // namespace

int main(int argc, char **argv)
{
	try {
		Options options = parse_options(argc, argv);
		rexc::Diagnostics diagnostics;
		rexc::ModuleLoadOptions load_options;
		load_options.package_paths = options.package_paths;

		auto parsed = rexc::parse_file_tree(options.input_path, diagnostics,
		                                    std::move(load_options));
		if (!parsed.ok()) {
			std::cerr << format_diagnostics(diagnostics, options.diagnostic_mode);
			return 1;
		}

		// FE-013: the CLI enforces the unsafe-block rule on user code.
		rexc::SemanticOptions semantic_options;
		semantic_options.enforce_unsafe_blocks = true;
		auto sema = rexc::analyze_module(parsed.module(), diagnostics, semantic_options);
		if (!sema.ok()) {
			std::cerr << format_diagnostics(diagnostics, options.diagnostic_mode);
			return 1;
		}

		auto ir = rexc::lower_to_ir(parsed.module());
		auto machine = rexc::codegen_target(options.target);
		rexc::CodegenResult codegen = [&]() {
			switch (machine) {
			case rexc::CodegenTarget::ARM64_MACOS:
				return rexc::emit_arm64_macos_assembly(ir, diagnostics);
			case rexc::CodegenTarget::ARM64_DRUNIX:
				return rexc::emit_arm64_drunix_assembly(ir, diagnostics);
			default:
				return rexc::emit_x86_assembly(ir, diagnostics, machine);
			}
		}();
		if (!codegen.ok()) {
			std::cerr << format_diagnostics(diagnostics, options.diagnostic_mode);
			return 1;
		}

		if (options.output_mode == OutputMode::Assembly) {
			write_file(options.output_path, codegen.assembly());
			return 0;
		}

		if (options.output_mode == OutputMode::Object) {
			std::string assembly_path = options.output_path + ".s.tmp";
			write_file(assembly_path, codegen.assembly());
			try {
				assemble_object(assembly_path, options.output_path, options.target);
			} catch (...) {
				remove_if_present(assembly_path);
				remove_if_present(options.output_path);
				throw;
			}
			remove_if_present(assembly_path);
			return 0;
		}

		if (options.output_mode == OutputMode::Executable) {
			std::string assembly_path = options.output_path + ".s.tmp";
			std::string object_path = options.output_path + ".o.tmp";
			std::string runtime_object_path;
			write_file(assembly_path, codegen.assembly());
			try {
				assemble_object(assembly_path, object_path, options.target);
				runtime_object_path = write_and_assemble_hosted_runtime(options.output_path,
				                                                        options.target);
				link_executable_object(object_path, runtime_object_path,
				                       options.output_path, options.target);
			} catch (...) {
				remove_if_present(assembly_path);
				remove_if_present(object_path);
				if (!runtime_object_path.empty())
					remove_if_present(runtime_object_path);
				remove_if_present(options.output_path);
				throw;
			}
			remove_if_present(assembly_path);
			remove_if_present(object_path);
			if (!runtime_object_path.empty())
				remove_if_present(runtime_object_path);
			return 0;
		}

		std::string assembly_path = options.output_path + ".s.tmp";
		std::string object_path = options.output_path + ".o.tmp";
		std::string runtime_object_path;
		write_file(assembly_path, codegen.assembly());
		try {
			assemble_object(assembly_path, object_path, options.target);
			runtime_object_path = write_and_assemble_hosted_runtime(options.output_path,
			                                                        options.target);
			link_drunix_object(object_path, options.output_path, runtime_object_path,
			                   options.drunix_root, options.target);
		} catch (...) {
			remove_if_present(assembly_path);
			remove_if_present(object_path);
			if (!runtime_object_path.empty())
				remove_if_present(runtime_object_path);
			remove_if_present(options.output_path);
			throw;
		}
		remove_if_present(assembly_path);
		remove_if_present(object_path);
		if (!runtime_object_path.empty())
			remove_if_present(runtime_object_path);
		return 0;
	} catch (const std::exception &err) {
		if (std::string(err.what()) == "usage") {
			std::cerr << "usage: rexc input.rx "
			             "[--package-path path] "
			             "[--target i386|i386-linux|i386-drunix|x86_64|x86_64-linux|arm64-macos] "
			             "[--diag=json] "
			             "[-S|-c|--drunix-root path] -o output\n";
			return 2;
		}
		std::cerr << "rexc: " << err.what() << '\n';
		return 1;
	}
}
