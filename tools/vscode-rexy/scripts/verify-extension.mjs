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
  const grammar = readJson("syntaxes/rexy.tmLanguage.json");

  const languages = packageJson.contributes?.languages ?? [];
  const grammars = packageJson.contributes?.grammars ?? [];
  const rexyLanguage = languages.find((language) => language.id === "rexy");
  const rexyGrammar = grammars.find((entry) => entry.language === "rexy");

  assert(packageJson.name === "rexy", "package name must be rexy");
  assert(packageJson.publisher === "rexc", "publisher must be rexc");
  assert(packageJson.engines?.vscode, "package must declare a VS Code engine");
  assert(rexyLanguage, "package must contribute the Rexy language");
  assert(rexyLanguage.extensions?.includes(".rx"), "Rexy language must handle .rx files");
  assert(rexyLanguage.aliases?.includes("Rexy"), "Rexy language must include Rexy alias");
  assert(
    rexyLanguage.configuration === "./language-configuration.json",
    "Rexy language must point at language-configuration.json",
  );
  assert(rexyGrammar, "package must contribute a Rexy grammar");
  assert(
    rexyGrammar.path === "./syntaxes/rexy.tmLanguage.json",
    "Rexy grammar must point at syntaxes/rexy.tmLanguage.json",
  );

  assert(languageConfig.comments?.lineComment === "//", "line comment must be //");
  assert(
    languageConfig.brackets?.some((pair) => pair[0] === "{" && pair[1] === "}"),
    "language config must include brace brackets",
  );

  assert(grammar.scopeName === "source.rexy", "grammar scopeName must be source.rexy");
  assert(grammar.repository?.keywords, "grammar must define keywords");
  assert(grammar.repository?.types, "grammar must define primitive types");
  assert(grammar.repository?.functions, "grammar must define functions");
  assert(grammar.repository?.operators, "grammar must define operators");
  assert(hasPattern(grammar.repository.literals, "string.quoted.double.rexy"), "grammar must highlight strings");
  assert(hasPattern(grammar.repository.literals, "constant.character.rexy"), "grammar must highlight chars");
  assert(hasPattern(grammar.repository.literals, "constant.numeric.integer.rexy"), "grammar must highlight integers");

  console.log("VS Code Rexy extension verification passed.");
} catch (error) {
  console.error(`VS Code Rexy extension verification failed: ${error.message}`);
  process.exit(1);
}
