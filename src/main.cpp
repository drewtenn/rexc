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
	if (argc != 5 || std::string(argv[2]) != "-S" || std::string(argv[3]) != "-o") {
		std::cerr << "usage: rexc input.rx -S -o output.s\n";
		return 2;
	}

	const std::string input_path = argv[1];
	const std::string output_path = argv[4];

	try {
		rexc::SourceFile source(input_path, read_file(input_path));
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
		auto codegen = rexc::emit_x86_assembly(ir, diagnostics);
		if (!codegen.ok()) {
			std::cerr << diagnostics.format();
			return 1;
		}

		write_file(output_path, codegen.assembly());
		return 0;
	} catch (const std::exception &err) {
		std::cerr << "rexc: " << err.what() << '\n';
		return 1;
	}
}
