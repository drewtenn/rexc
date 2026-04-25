// Rexc command-line driver.
//
// main.cpp owns process-level concerns: command-line parsing, file I/O, stage
// sequencing, diagnostic printing, and exit status. The actual compiler work is
// delegated to parse, sema, IR lowering, and x86 codegen so the CLI remains a
// thin coordinator from .rx source text to emitted assembly.
#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
	std::string input_path;
	std::string output_path;
	rexc::CodegenTarget target = rexc::CodegenTarget::I386;
	bool emit_assembly = false;
};

rexc::CodegenTarget parse_target(const std::string &target)
{
	if (target == "i386")
		return rexc::CodegenTarget::I386;
	if (target == "x86_64")
		return rexc::CodegenTarget::X86_64;
	throw std::runtime_error("unknown target: " + target);
}

Options parse_options(int argc, char **argv)
{
	Options options;
	if (argc < 5)
		throw std::runtime_error("usage");

	options.input_path = argv[1];
	for (int i = 2; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-S") {
			options.emit_assembly = true;
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
		throw std::runtime_error("usage");
	}

	if (options.input_path.empty() || options.output_path.empty() || !options.emit_assembly)
		throw std::runtime_error("usage");
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
		auto codegen = rexc::emit_x86_assembly(ir, diagnostics, options.target);
		if (!codegen.ok()) {
			std::cerr << diagnostics.format();
			return 1;
		}

		write_file(options.output_path, codegen.assembly());
		return 0;
	} catch (const std::exception &err) {
		if (std::string(err.what()) == "usage") {
			std::cerr << "usage: rexc input.rx [--target i386|x86_64] -S -o output.s\n";
			return 2;
		}
		std::cerr << "rexc: " << err.what() << '\n';
		return 1;
	}
}
