// Rexc command-line driver.
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
#include "rexc/source.hpp"
#include "rexc/stdlib.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

enum class OutputMode {
	Assembly,
	Object,
	Executable,
	DrunixExecutable,
};

struct Options {
	std::string input_path;
	std::string output_path;
	std::string drunix_root;
	rexc::CodegenTarget target =
#if defined(__APPLE__)
		rexc::CodegenTarget::ARM64_MACOS;
#else
		rexc::CodegenTarget::I386;
#endif
	OutputMode output_mode = OutputMode::Assembly;
	bool output_mode_selected = false;
};

rexc::CodegenTarget parse_target(const std::string &target)
{
	if (target == "i386" || target == "i386-linux" || target == "i386-elf" ||
	    target == "i386-drunix" || target == "i686-linux" ||
	    target == "i686-unknown-linux-gnu")
		return rexc::CodegenTarget::I386;
	if (target == "x86_64" || target == "x86_64-linux" || target == "x86_64-elf" ||
	    target == "x86_64-unknown-linux-gnu")
		return rexc::CodegenTarget::X86_64;
	if (target == "arm64-macos" || target == "arm64-apple-darwin" ||
	    target == "aarch64-apple-darwin")
		return rexc::CodegenTarget::ARM64_MACOS;
	throw std::runtime_error("unknown target: " + target);
}

void select_output_mode(Options &options, OutputMode mode)
{
	if (options.output_mode_selected && options.output_mode != mode)
		throw std::runtime_error("usage");
	options.output_mode = mode;
	options.output_mode_selected = true;
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
		if (arg == "--target" && i + 1 < argc) {
			options.target = parse_target(argv[++i]);
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
	return options;
}

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

void assemble_object(const std::string &assembly_path, const std::string &object_path,
                     rexc::CodegenTarget target)
{
	if (target == rexc::CodegenTarget::ARM64_MACOS) {
		if (!command_exists("as"))
			throw std::runtime_error("no Apple assembler found");
		std::string command = "as -arch arm64 -o " + shell_quote(object_path) +
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

	std::string mode = target == rexc::CodegenTarget::I386 ? "--32" : "--64";
	std::string command = assembler + " " + mode + " -o " +
	                      shell_quote(object_path) + " " +
	                      shell_quote(assembly_path);
	run_tool(command, "assembler failed");
}

std::string write_and_assemble_hosted_runtime(const std::string &output_path,
                                              rexc::CodegenTarget target)
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
                        const std::string &drunix_root, rexc::CodegenTarget target)
{
	if (target != rexc::CodegenTarget::I386)
		throw std::runtime_error("Drunix linking currently supports only the i386 target");

	std::string user_dir = drunix_root + "/user";
	std::string linker_script = user_dir + "/user.ld";
	std::string crt0 = user_dir + "/lib/crt0.o";
	std::string libc = user_dir + "/lib/libc.a";
	require_file(linker_script, "Drunix linker script");
	require_file(crt0, "Drunix startup object");
	require_file(libc, "Drunix runtime archive");

	std::string linker = find_tool("x86_64-elf-ld", "ld", "ELF linker");
	std::string command = linker + " -m elf_i386 -T " +
	                      shell_quote(linker_script) + " -o " +
	                      shell_quote(output_path) + " " + shell_quote(crt0) +
	                      " " + shell_quote(object_path) + " " +
	                      shell_quote(libc);
	run_tool(command, "Drunix link failed");
}

void link_darwin_arm64_object(const std::string &object_path,
                              const std::string &runtime_object_path,
                              const std::string &output_path,
                              rexc::CodegenTarget target)
{
	if (target != rexc::CodegenTarget::ARM64_MACOS)
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
		           "\tcall main\n"
		           "\tmovl %eax, %ebx\n"
		           "\tmovl $1, %eax\n"
		           "\tint $0x80\n");
		return;
	}

	write_file(assembly_path,
	           ".globl _start\n"
	           "_start:\n"
	           "\tcall main\n"
	           "\tmovl %eax, %edi\n"
	           "\tmovq $60, %rax\n"
	           "\tsyscall\n");
}

void link_cross_elf_object(const std::string &object_path,
                           const std::string &runtime_object_path,
                           const std::string &output_path,
                           rexc::CodegenTarget target)
{
	if (target == rexc::CodegenTarget::ARM64_MACOS)
		throw std::runtime_error("internal error: ARM64 object reached ELF linker");
	if (!command_exists("x86_64-elf-as") || !command_exists("x86_64-elf-ld"))
		throw std::runtime_error("no x86_64-elf-as/x86_64-elf-ld toolchain found");

	std::string startup_assembly = output_path + ".crt0.s.tmp";
	std::string startup_object = output_path + ".crt0.o.tmp";
	write_cross_elf_startup(startup_assembly, target);
	try {
		assemble_object(startup_assembly, startup_object, target);
		std::string linker_mode = target == rexc::CodegenTarget::I386 ? "elf_i386" : "elf_x86_64";
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
                          rexc::CodegenTarget target)
{
	if (target == rexc::CodegenTarget::ARM64_MACOS)
		throw std::runtime_error("internal error: ARM64 object reached x86 linker");
	if (command_succeeds("uname -s | grep -q Darwin")) {
		link_cross_elf_object(object_path, runtime_object_path, output_path, target);
		return;
	}

	std::string linker = find_tool("clang", "cc", "C linker driver");
	std::string mode = target == rexc::CodegenTarget::I386 ? "-m32" : "-m64";
	std::string command = linker + " " + mode + " " + shell_quote(object_path);
	if (!runtime_object_path.empty())
		command += " " + shell_quote(runtime_object_path);
	command += " -o " + shell_quote(output_path);
	run_tool(command, "host executable link failed");
}

void link_executable_object(const std::string &object_path,
                            const std::string &runtime_object_path,
                            const std::string &output_path,
                            rexc::CodegenTarget target)
{
	if (target == rexc::CodegenTarget::ARM64_MACOS) {
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

} // namespace

int main(int argc, char **argv)
{
	try {
		Options options = parse_options(argc, argv);
		rexc::SourceFile source(options.input_path, read_file(options.input_path));
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
		rexc::CodegenResult codegen =
			options.target == rexc::CodegenTarget::ARM64_MACOS
				? rexc::emit_arm64_macos_assembly(ir, diagnostics)
				: rexc::emit_x86_assembly(ir, diagnostics, options.target);
		if (!codegen.ok()) {
			std::cerr << diagnostics.format();
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
		write_file(assembly_path, codegen.assembly());
		try {
			assemble_object(assembly_path, object_path, options.target);
			link_drunix_object(object_path, options.output_path, options.drunix_root,
			                   options.target);
		} catch (...) {
			remove_if_present(assembly_path);
			remove_if_present(object_path);
			remove_if_present(options.output_path);
			throw;
		}
		remove_if_present(assembly_path);
		remove_if_present(object_path);
		return 0;
	} catch (const std::exception &err) {
		if (std::string(err.what()) == "usage") {
			std::cerr << "usage: rexc input.rx "
			             "[--target i386|i386-linux|x86_64|x86_64-linux|arm64-macos] "
			             "[-S|-c|--drunix-root path] -o output\n";
			return 2;
		}
		std::cerr << "rexc: " << err.what() << '\n';
		return 1;
	}
}
