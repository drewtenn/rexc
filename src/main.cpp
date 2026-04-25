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
	DarwinExecutable,
	DrunixExecutable,
};

struct Options {
	std::string input_path;
	std::string output_path;
	std::string drunix_root;
	rexc::CodegenTarget target = rexc::CodegenTarget::I386;
	OutputMode output_mode = OutputMode::Assembly;
	bool output_mode_selected = false;
};

rexc::CodegenTarget parse_target(const std::string &target)
{
	if (target == "i386")
		return rexc::CodegenTarget::I386;
	if (target == "x86_64")
		return rexc::CodegenTarget::X86_64;
	if (target == "arm64-macos" || target == "aarch64-apple-darwin")
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
		if (options.target == rexc::CodegenTarget::ARM64_MACOS) {
			options.output_mode = OutputMode::DarwinExecutable;
			options.output_mode_selected = true;
		} else {
			throw std::runtime_error("usage");
		}
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
                              const std::string &output_path,
                              rexc::CodegenTarget target)
{
	if (target != rexc::CodegenTarget::ARM64_MACOS)
		throw std::runtime_error("Darwin linking currently supports only the arm64-macos target");
	if (!command_exists("clang"))
		throw std::runtime_error("no clang linker driver found");

	std::string command = "clang -arch arm64 " + shell_quote(object_path) +
	                      " -o " + shell_quote(output_path);
	run_tool(command, "Darwin link failed");
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

		if (options.output_mode == OutputMode::DarwinExecutable) {
			std::string assembly_path = options.output_path + ".s.tmp";
			std::string object_path = options.output_path + ".o.tmp";
			write_file(assembly_path, codegen.assembly());
			try {
				assemble_object(assembly_path, object_path, options.target);
				link_darwin_arm64_object(object_path, options.output_path,
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
			std::cerr << "usage: rexc input.rx [--target i386|x86_64|arm64-macos] "
			             "[-S|-c|--drunix-root path] -o output\n";
			return 2;
		}
		std::cerr << "rexc: " << err.what() << '\n';
		return 1;
	}
}
