import fs from "node:fs";
import path from "node:path";
import process from "node:process";

const root = path.resolve(new URL("../", import.meta.url).pathname);

function readJson(relativePath) {
  const fullPath = path.join(root, relativePath);
  if (!fs.existsSync(fullPath)) {
    throw new Error(`Missing ${relativePath}`);
  }

  try {
    return JSON.parse(fs.readFileSync(fullPath, "utf8"));
  } catch (error) {
    throw new Error(`Invalid JSON in ${relativePath}: ${error.message}`);
  }
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function hasPattern(repository, name) {
  return repository.patterns.some((pattern) => pattern.name === name);
}

try {
  const packageJson = readJson("package.json");
  const languageConfig = readJson("language-configuration.json");
  const grammar = readJson("syntaxes/rexc.tmLanguage.json");

  const languages = packageJson.contributes?.languages ?? [];
  const grammars = packageJson.contributes?.grammars ?? [];
  const rexcLanguage = languages.find((language) => language.id === "rexc");
  const rexcGrammar = grammars.find((entry) => entry.language === "rexc");

  assert(packageJson.name === "rexc", "package name must be rexc");
  assert(packageJson.publisher === "rexc", "publisher must be rexc");
  assert(packageJson.engines?.vscode, "package must declare a VS Code engine");
  assert(rexcLanguage, "package must contribute the rexc language");
  assert(rexcLanguage.extensions?.includes(".rx"), "rexc language must handle .rx files");
  assert(rexcLanguage.aliases?.includes("Rex"), "rexc language must include Rex alias");
  assert(rexcLanguage.aliases?.includes("Rexc"), "rexc language must include Rexc alias");
  assert(
    rexcLanguage.configuration === "./language-configuration.json",
    "rexc language must point at language-configuration.json",
  );
  assert(rexcGrammar, "package must contribute a rexc grammar");
  assert(
    rexcGrammar.path === "./syntaxes/rexc.tmLanguage.json",
    "rexc grammar must point at syntaxes/rexc.tmLanguage.json",
  );

  assert(languageConfig.comments?.lineComment === "//", "line comment must be //");
  assert(
    languageConfig.brackets?.some((pair) => pair[0] === "{" && pair[1] === "}"),
    "language config must include brace brackets",
  );

  assert(grammar.scopeName === "source.rexc", "grammar scopeName must be source.rexc");
  assert(grammar.repository?.keywords, "grammar must define keywords");
  assert(grammar.repository?.types, "grammar must define primitive types");
  assert(grammar.repository?.functions, "grammar must define functions");
  assert(grammar.repository?.operators, "grammar must define operators");
  assert(hasPattern(grammar.repository.literals, "string.quoted.double.rexc"), "grammar must highlight strings");
  assert(hasPattern(grammar.repository.literals, "constant.character.rexc"), "grammar must highlight chars");
  assert(hasPattern(grammar.repository.literals, "constant.numeric.integer.rexc"), "grammar must highlight integers");

  console.log("VS Code Rexc extension verification passed.");
} catch (error) {
  console.error(`VS Code Rexc extension verification failed: ${error.message}`);
  process.exit(1);
}
