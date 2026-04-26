#include "rexc/stdlib.hpp"

#include "rexc/codegen_arm64.hpp"
#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"

#include "alloc/library.hpp"
#include "core/library.hpp"
#include "source.hpp"
#include "std/library.hpp"
#include "sys/runtime.hpp"

#include <string>

namespace rexc::stdlib {
namespace {

void append_functions(std::vector<FunctionDecl> &target,
                      const std::vector<FunctionDecl> &source)
{
	target.insert(target.end(), source.begin(), source.end());
}

std::string portable_stdlib_assembly(CodegenTarget target)
{
	Diagnostics diagnostics;
	SourceFile source("stdlib.rx", portable_stdlib_source());
	auto parsed = parse_source(source, diagnostics);
	if (!parsed.ok())
		return "# failed to parse stdlib.rx\n# " + diagnostics.format() + "\n";

	SemanticOptions semantic_options;
	semantic_options.include_stdlib_prelude = false;
	auto sema = analyze_module(parsed.module(), diagnostics, semantic_options);
	if (!sema.ok())
		return "# failed to analyze stdlib.rx\n# " + diagnostics.format() + "\n";

	LowerOptions lower_options;
	lower_options.include_stdlib_prelude = false;
	auto lowered = lower_to_ir(parsed.module(), lower_options);

	CodegenResult emitted =
		target == CodegenTarget::ARM64_MACOS
			? emit_arm64_macos_assembly(lowered, diagnostics)
			: emit_x86_assembly(lowered, diagnostics, target);
	if (!emitted.ok())
		return "# failed to emit stdlib.rx\n# " + diagnostics.format() + "\n";
	return emitted.assembly();
}

std::string sys_runtime_assembly(CodegenTarget target)
{
	switch (target) {
	case CodegenTarget::I386:
		return i386_hosted_runtime_assembly();
	case CodegenTarget::X86_64:
		return x86_64_hosted_runtime_assembly();
	case CodegenTarget::ARM64_MACOS:
		return arm64_macos_hosted_runtime_assembly();
	}
	return "";
}

} // namespace

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions = [] {
		std::vector<FunctionDecl> result;
		append_functions(result, core::prelude_functions());
		append_functions(result, alloc::prelude_functions());
		append_functions(result, std_layer::prelude_functions());
		return result;
	}();
	return functions;
}

const FunctionDecl *find_prelude_function(const std::string &name)
{
	for (const auto &function : prelude_functions()) {
		if (function.name == name)
			return &function;
	}
	return nullptr;
}

std::string hosted_runtime_assembly(CodegenTarget target)
{
	return sys_runtime_assembly(target) + "\n" + portable_stdlib_assembly(target);
}

} // namespace rexc::stdlib
