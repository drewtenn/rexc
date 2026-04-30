# Rexy Package Manager — Product Requirements Document

**Codename:** `rxy`
**Status:** Draft v1
**Author:** Drew Tennenbaum
**Date:** 2026-04-29
**Companion to:** `docs/prd.md` (Rexy language PRD), `docs/roadmap.md` (compiler roadmap)

---

## 1. Executive Summary

`rxy` is the package manager and build tool for the Rexy systems language — a Cargo-equivalent for `rexc`. It replaces `Makefile`/CMake at the project boundary with a declarative `Rexy.toml` manifest, a reproducible `Rexy.lock` lockfile, and a small set of opinionated commands (`rxy build`, `rxy test`, `rxy install`, `rxy publish`).

**Key value:** every Rexy project — from `hello.rx` to the self-hosted `rexc` itself — uses the same build, dependency, and distribution workflow. There is no second build system to learn, no recursive `make`, no per-platform `Makefile.osx`, and no "every project re-invents its build" failure mode that the language PRD calls out as the C++ disaster (PRD §C-6, §NG-4).

**Distribution model:** git-based registry from day one, Go-modules-style. No centralized server to host, moderate, or pay for in v1. A small registry-index repo (Cargo-style) provides discoverability; package sources live in their authors' git repos. Reproducibility is enforced by content checksums in `Rexy.lock`, not by trusting git tags.

**This PRD elevates** what was previously deferred as `NG-4` and listed as Phase 8+ stretch in the roadmap into a concrete, scoped, shippable v1.

---

## 2. Problem Statement

### 2.1 The Makefile pain Rexy users hit today

Every Rexy project today reaches one of these dead-ends within a few hundred LOC:

| # | Pain | Impact |
|---|---|---|
| **P-1** | **Multi-file projects need hand-rolled `Makefile` rules.** Every project invents a different convention for where `.rx` lives, how to invoke `rexc`, and how to wire `--package-path`. | The compiler's own `examples/modules/` is illustrative-only; nothing teaches a real project how to scale. |
| **P-2** | **No way to depend on someone else's Rexy code.** Reuse is copy-paste or `git submodule`. Both rot. | Library ecosystem cannot start until this is solved. |
| **P-3** | **Reproducibility is `git status`.** No lockfile, no checksum, no "this build today equals this build in 2030." | Systems software ships for a decade. Build drift is unacceptable. |
| **P-4** | **Cross-compilation is a CLI invocation.** `rexc --target=x86_64-linux …` works, but there is no project-level way to say "build for these three triples on `rxy build`." | The language PRD makes cross-compile a P0 promise (FR-020/021/022/023). The build tool must surface it. |
| **P-5** | **Tests are ad-hoc shell.** `tests/` runs via custom scripts in CMake/CTest. There is no `rxy test` story for downstream projects. | Library authors cannot ship tested libraries; consumers cannot run tests on dependencies. |
| **P-6** | **Stdlib paths and user modules use different machinery.** Roadmap Phase 7 notes: "stdlib `.rx` files are still embedded by CMake rather than loaded through ordinary module search." | Users perceive stdlib as magic and cannot model it. |

### 2.2 Why now (and why not earlier)

The original language PRD listed `NG-4: No package manager in v1` with this rationale:

> Cargo is excellent and *also* drove Rust's ecosystem fragmentation around async. We are not racing to recreate that.

That decision was correct at the time it was written. Three things have changed:

1. **Module system shipped.** Roadmap Phase 7 (real module loading) is complete. `mod foo;` and `use foo::bar;` work with `--package-path`. The substrate exists.
2. **`rexc` itself wants to scale beyond a single CMake build** — the self-hosted compiler (Phase 7 of language roadmap) and Drunix proving ground both need a real multi-crate story.
3. **The fragmentation risk is *worse* without a package manager.** Zig's late-game (0.11.0+) package manager landed on top of years of ad-hoc `build.zig` glue. The longer Rexy waits, the more incompatible "homegrown" build conventions accumulate.

The language is no longer "not stable enough" — Phase 1–6 of the language roadmap has nailed down structs, generics, allocators, comptime, async, and toolchain. Now is the moment.

### 2.3 What "good" looks like

A new Rexy user can run `rxy new hello && cd hello && rxy run` and get a working binary in <10 seconds, no `cmake`, no `make`, no `Makefile`. A library author can run `rxy publish` and have downstream consumers `rxy add their/lib` to depend on it. A 6-month-old project rebuilds bit-identically from `Rexy.lock` against its original git sources.

---

## 3. Goals & Metrics

### 3.1 P0 (must-have for v1)

| # | Goal | Success Metric |
|---|---|---|
| **G0-1** | **Replace `Makefile` for the common Rexy project.** | The `examples/modules/` project, the existing `arena_vec_demo`/`hashmap_demo`, and `rexc` itself all build with `rxy build` — no `Makefile`, no `CMakeLists.txt` at the project root. |
| **G0-2** | **`Rexy.toml` + `Rexy.lock` reproducibility.** | Building the same `Rexy.lock` against the same git registry yields a byte-identical binary on the same host (modulo timestamps). |
| **G0-3** | **Git-based dependencies that work.** | A project can declare `foo = { git = "https://github.com/acme/foo.rx", version = "0.4" }`, and `rxy build` resolves, fetches, checksums, locks, and compiles it. |
| **G0-4** | **Build scripts in Rexy.** | A package can ship a `build.rx` that runs before its main compilation, can emit link directives, generate `.rx` source, and probe the host. |
| **G0-5** | **`rxy publish` and `rxy install` round-trip.** | An author publishes `foo 0.1.0`; a consumer installs and uses it. The registry-index repo records the binding and `Rexy.lock` pins the commit + content checksum. |

### 3.2 P1 (target for v1, can slip)

| # | Goal | Success Metric |
|---|---|---|
| **G1-1** | **Workspaces / monorepos.** | `rexc` itself is restructured as a `[workspace]` with members for compiler, stdlib, tools — all built by one `rxy build`. |
| **G1-2** | **Cross-compile parity with `rexc`.** | `rxy build --target=x86_64-linux` produces the same artifact as `rexc --target=x86_64-linux` does today, but project-aware. |
| **G1-3** | **`rxy test` runs `[[targets.test]]`.** | Library authors can ship a test suite consumers can run. |
| **G1-4** | **Offline/locked builds.** | `rxy build --locked --offline` succeeds when sources are cached, fails clearly when they are not. |
| **G1-5** | **Stdlib through ordinary module search.** | Roadmap Phase 7's known limit ("stdlib embedded by CMake") is removed; stdlib ships as a built-in package. |

### 3.3 P2 (post-v1)

- Centralized registry/discovery website (rexy.dev or similar).
- SBOM / sigstore / supply-chain attestation.
- Binary-cache / pre-built artifact distribution.
- Profile-guided optimization.
- Plugin/subcommand discovery (`rxy-foo` on PATH → `rxy foo`).
- Sandboxed build script execution.
- Private registry mirrors / `[source.replace-with]` overrides at scale.

### 3.4 Top-line success metrics

| Metric | Baseline (today) | v1 target |
|---|---|---|
| Time to first `Hello, world` build for a new user | ~10 minutes (cmake + make + figuring out `--package-path`) | **<60 seconds** (`rxy new` → `rxy run`) |
| `Makefile` lines in a typical Rexy project | ~50–200 | **0** |
| % of canonical Rexy demos buildable by `rxy build` | 0% | **100%** |
| Reproducibility: same `Rexy.lock` → same artifact | not measured | **byte-identical (modulo timestamps), verified in CI** |
| Cold cache → hot dep tree resolve time, 10 deps | n/a | **<2s online, <100ms offline** |

---

## 4. Non-Goals

These are explicit *no*s for v1. Drift requires re-opening this PRD.

| # | Non-goal | Reason |
|---|---|---|
| **NG-1** | **No centralized package server (no "rexygems.io").** | Operating, moderating, paying for, and trust-rooting a central server is a non-trivial side quest. Git registries (Go modules, Deno) are sufficient for v1. P2 may revisit. |
| **NG-2** | **No SAT-based dependency solver.** | Cargo's resolver is excellent and also a tar pit. v1 uses simple "highest compatible semver tag" within the manifest's constraints, with hard failure on incompatible major versions. To keep this honest, v1 *constrains the input space*: no optional dependencies, no platform-overrides that introduce new packages (only different versions of already-required ones), no feature flags, no multi-major coexistence. If the resolver ever needs to "decide," v1 prefers a hard error with reasoned output over silent SAT-style backtracking. |
| **NG-3** | **No Cargo-style feature unification.** | The single biggest design regret in Cargo. v1 uses additive *capabilities* with no negative features and printable resolution reasons. |
| **NG-4** | **No install-time arbitrary script execution.** | npm's lifecycle scripts are a documented supply-chain hazard. Build scripts in Rexy run *only* during `rxy build`, never during `rxy install`, and only for the package being built (not transitively unless the user opts in). |
| **NG-5** | **No vendored C/C++ build system.** | `rxy` does not subsume CMake/autotools/meson for native C deps in v1. C interop happens through `extern "C"` and a manifest-declared `links = [...]`; the host system provides the library. P2 may add system-library probing helpers. |
| **NG-6** | **No macros around manifest fields.** | `Rexy.toml` is *declarative*. No template strings, no environment-variable expansion in fields, no conditional includes outside `[platform.<triple>]` blocks. |
| **NG-7** | **No replacement for `rexc`.** | `rxy` invokes `rexc`. It does not reimplement parsing, semantic analysis, or codegen. The compiler stays the source of truth. |
| **NG-8** | **No "lockfile-less" mode.** | Every `rxy build` reads or writes `Rexy.lock`. There is no "just resolve the latest" toggle. Reproducibility is mandatory. |
| **NG-9** | **No Windows host support in v1.** | Mirrors the language PRD's NG-8 (Windows is host-only and recipe-not-yet-documented). `rxy` targets macOS/Linux hosts in v1. |

---

## 5. User Personas

### Persona A — "The Rexy app developer"
**Profile:** writes a binary in Rexy that depends on 2–8 libraries.
**Cares about:** `rxy new`, `rxy add foo`, `rxy run`, `rxy build --release`. Clear errors when a dep fails to resolve.
**Anti-cares:** SAT solvers, registry trust roots, build script sandboxing.

### Persona B — "The library author"
**Profile:** writes a reusable Rexy library, hosts it on GitHub, wants others to depend on it.
**Cares about:** `rxy publish` working without ceremony, semver enforcement, `rxy test` running their suite under consumers' configurations.
**Anti-cares:** Setting up a central registry account; uploading tarballs.

### Persona C — "The Rexy compiler maintainer (you)"
**Profile:** ships `rexc` itself; wants the compiler's own build, tests, examples, and stdlib to live under `rxy`.
**Cares about:** Workspaces, cross-compile, building the existing `examples/` and `tests/` trees as a `rxy` workspace, replacing CMake at the project boundary while keeping the existing CMake usage *inside* `rexc`'s own build for ANTLR.
**Anti-cares:** Surface area outside what `rexc` actually exercises.

---

## 6. Functional Requirements

### 6.1 Manifest (`Rexy.toml`)

- **FR-001** Every package has a `Rexy.toml` at its root. Required fields: `[package] name, version, edition`. The `edition` field is a year-string (e.g., `"2026"`) that groups language breaking-change opt-ins, Rust-style. The first edition is `"2026"`. Recommended fields: `description, license, repository, authors`. `rxy build` checks `edition` against the installed `rexc` capability set and errors with a clear remediation if mismatched (see R-5).
- **FR-002** `[dependencies]`, `[dev-dependencies]`, and `[platform.<triple>.dependencies]` tables. Each entry is either a semver string (`"0.4"`) or an inline table with `git = "..."`, `version = "..."`, optional `rev = "..."` / `tag = "..."` / `branch = "..."`.
- **FR-003** `[build]` table with `script = "build.rx"` (optional), `links = ["ssl", "z"]` (optional, declares native-library link names), `include = ["src/**/*.rx"]` (optional, source globs).
- **FR-004** `[targets.lib]` (single, optional) and `[[targets.bin]]` / `[[targets.test]]` / `[[targets.example]]` (multiple, optional). Each has a `path` and a `name`.
- **FR-005** `[profile.dev]` and `[profile.release]` (and arbitrary `[profile.<name>]`) with knobs: `opt-level`, `debug`, `lto`, `strip`, `warnings`, `target-cpu`. Profile selection: `rxy build` = dev, `rxy build --release` = release, `rxy build --profile=foo` = explicit.
- **FR-006** `[workspace]` table with `members = ["crates/*"]`. A workspace root has a `Rexy.toml` with `[workspace]` and may also have its own `[package]` (Cargo-style "root package + workspace"). Inheritance, mirroring Cargo's full surface:
  - **`[workspace.dependencies]`** — members opt-in per dependency via `foo = { workspace = true }` or `foo.workspace = true`. Inherited entries may override `features` / `optional` at the member level but not `version` or `source`.
  - **`[workspace.package]`** — metadata fields (`version`, `license`, `authors`, `repository`, `edition`, `description`) members opt-in via `version.workspace = true` etc.
  - **`[workspace.profile.<name>]`** — applies to every member by default; a member's `[profile.<name>]` overrides at the member level.
  - **`[workspace.lints]`** — reserved for P2.
  Footguns documented: a member silently picking up a workspace dep it didn't expect (mitigation: opt-in only); profile overrides being shadowed in confusing ways (mitigation: `rxy build -v` prints the resolved profile chain). (P1)
- **FR-007** `Rexy.toml` parsing rejects unknown top-level keys with a clear error. Forward-compat: unknown keys *inside* a known table emit a warning, not an error, gated on `edition`.
- **FR-008** No environment-variable expansion, no `include`-style imports, no template syntax in manifest values. (Enforces NG-6.)

### 6.2 Lockfile (`Rexy.lock`)

- **FR-009** `Rexy.lock` is generated/updated on every `rxy build`, `rxy add`, `rxy update`, `rxy install`. It is checked into version control for binary projects; libraries should commit it but consumers ignore the library's lockfile and use their own.
- **FR-010** Each entry binds: `name`, resolved `version`, source (`registry` or `git+url`), `commit` (40-char SHA-1 or SHA-256-tree object), `checksum` (SHA-256 of the resolved source tree), and `dependencies` (flat list of `name@version`).
- **FR-011** `rxy build --locked` fails if the lockfile would be modified. CI mode.
- **FR-012** `rxy build --offline` fails if any source is not in the local cache. Implies `--locked`.
- **FR-013** Lockfile mismatch on a previously-locked entry (e.g., upstream force-pushed a tag) is a hard error with a clear "the registry returned a different checksum than the lockfile pinned" message and remediation steps. Mitigates Go-modules' early "disappearing/moving tag" failures.

### 6.3 Resolution & Registry

- **FR-014** Default registry is a git repo at a configurable URL (e.g., `https://github.com/rexy-lang/registry`). Layout: `packages/<2-char-prefix>/<name>/<version>.toml`, each entry recording `{ git: <url>, rev: <commit>, checksum: <sha256> }`. Cargo-index style.
- **FR-015** Direct git dependencies (without going through the index) are first-class and work offline-first. Useful for private code, forks, in-development packages.
- **FR-016** Resolution algorithm: highest semver tag matching each dependency's constraint that is also pairwise-compatible with siblings. Two majors of the same crate are *errors* in v1, not silently coexisting. Reasoned, deterministic, no SAT.
- **FR-017** Multiple registries are supported via `[source.<name>]` blocks. (P1) v1 ships with a single default registry; the design must not preclude the second one.
- **FR-018** Tag immutability: once `(name, version)` is published with checksum `C`, the registry must reject a subsequent `version`-equal entry with a different checksum. The CLI surfaces this as a clear error if encountered locally.

### 6.4 Cache layout (`~/.rxy/`)

- **FR-019** The global cache lives at `$REXY_HOME` (default `~/.rxy/`). Subdirectories: `registry/<name>.git`, `git/db/<url-hash>.git`, `src/<name>/<version>/<commit>/`, `bin/`. Cache contents are content-addressable and immutable.
- **FR-020** `rxy cache prune` removes orphaned entries; `rxy cache clean` removes everything. The cache is never written-to during a build except to add new immutable content.

### 6.5 Build scripts

- **FR-021** A package's `build.rx` (when declared in `[build] script`) is compiled with the same `rexc` as the rest of the package, then executed before main compilation begins.
- **FR-022** Build scripts have a constrained, typed API exposed via a `build` module: `build.emit_link_lib(name)`, `build.emit_link_search_path(path)`, `build.rerun_if_changed(path)`, `build.rerun_if_env_changed(var)`, `build.generate(out_path, contents)`, `build.host_target()`, `build.target_triple()`, `build.profile_name()`, `build.out_dir()`. Output instructions are emitted on `stdout` as `rxy:<key>=<value>` lines (Cargo-style).
- **FR-023** Build scripts run *only* during `rxy build` of the owning package. Never during `rxy install`. Never for transitive dependencies unless the consumer's `Rexy.toml` opts in via `allow-build-scripts = ["foo", "bar"]`. (Enforces NG-4.)
- **FR-024** `rxy build --no-build-scripts` skips all build scripts (CI / audit mode). Packages that *require* their build script to compile fail clearly under this flag.
- **FR-025** Build scripts run in the package's source directory with `OUT_DIR` set to a per-build scratch dir under `target/`. They have full filesystem and process access in v1; sandboxing is P2.

### 6.6 Commands

- **FR-026** `rxy new <name> [--lib|--bin]` creates a new project skeleton.
- **FR-027** `rxy init` initializes the current directory.
- **FR-028** `rxy build [--release] [--target=<triple>] [--profile=<name>] [--locked] [--offline]` compiles all `[targets]` of the current package (or workspace).
- **FR-029** `rxy run [--bin <name>] [-- <args...>]` builds and runs a binary.
- **FR-030** `rxy test [<name-pattern>] [--target=<triple>]` builds and runs tests.
- **FR-031** `rxy add <spec>` adds a dependency to `Rexy.toml` and updates `Rexy.lock`.
- **FR-032** `rxy remove <name>` removes a dependency.
- **FR-033** `rxy update [<name>]` re-resolves the lockfile. With no argument: every dep up to its constraint. With `<name>`: just that dep.
- **FR-034** `rxy install <spec>` installs a binary package globally to `$REXY_HOME/bin/`. Binary name conflicts are reported (not silently overwritten); user must pass `--force` or `--rename`. **`rxy install` builds from source by default and therefore *does* run that package's `build.rx` if declared.** This is consistent with FR-023's rule that build scripts run during compilation, not during dependency *fetch*. The user is the consumer of `rxy install` and is making a deliberate choice; transitive dependency build scripts still require the installed package's `Rexy.toml` to allow-list them. This intentional carve-out is documented prominently in `rxy install --help` to avoid surprising users.
- **FR-035** `rxy publish` validates the current package, computes its checksum, and pushes a new entry to the configured registry index. (Implementation: a git commit + push to the registry index repo, gated on registry-side write permission.) Publishing requires a `[package] namespace = "..."` claimed by the publisher; namespaces are first-come-first-served and recorded in the registry-index repo's `OWNERS` file. Squatting/transfer policy is documented separately under registry governance (see R-12).
- **FR-036** `rxy yank <pkg>@<version>` marks a published version as yanked. Yanked versions remain available to existing `Rexy.lock` files but are skipped by fresh resolution. `rxy yank --reason=<text>` records a human-readable reason (security advisory, broken release, etc.). Mirrors Cargo behavior; addresses the "what if foo 1.2.0 is malware" scenario.
- **FR-037** `rxy migrate` analyzes a project that uses `--package-path` flags, ad-hoc `Makefile` rules, or `CTest` tests, and writes a best-effort `Rexy.toml` + suggested directory layout. Output is reviewed by the user before commit; the command does not modify source code. Targets canonical Rexy demos in `examples/` and `tests/`.
- **FR-038** `rxy vendor` copies all locked dependency sources into `vendor/` under the project root, and rewrites resolution so subsequent `rxy build --offline` reads exclusively from `vendor/`. Provides the "rebuilds in 2030 even if GitHub is gone" guarantee that checksums alone cannot.
- **FR-039** `rxy fmt`, `rxy doc`, `rxy bench` are reserved subcommand names. Implementations are P2.
- **FR-040** Every command supports `--manifest-path=<path>` to operate on a non-cwd manifest, `-q`/`-v` for verbosity, and `--json` for machine-readable output.

### 6.7 Diagnostics

- **FR-041** Manifest parse errors report file, line, column, and a one-line "expected X, found Y" message. No stack traces.
- **FR-042** Resolution failures surface the *reason chain*: "package `foo 0.4` requires `bar ^1.2`, but `bar 2.0` is also required by `baz 0.1`. No version of `bar` satisfies both." Cargo-quality.
- **FR-043** Build failures pass through `rexc` diagnostics unchanged; `rxy` adds a single header line ("compiling foo v0.4.1 (target: bin/server)") and trailer line.
- **FR-044** All diagnostics honor the language PRD's diagnostic budget (FR-024: 8-line cap unless `-v`).
- **FR-045** When a transitive dependency requires a build script that the consumer has not allow-listed (FR-023), the diagnostic is *actionable*: "`crate foo 1.2.0` (dependency of `bar 0.3.0`, of yours) ships a `build.rx`. To allow it, add `foo` to `allow-build-scripts` in your `Rexy.toml`. To audit it first, run `rxy show-build-script foo@1.2.0`. To opt out and likely break the build, do nothing." Avoids the "principled but broken-feeling" UX the adversarial review flagged as the #1 complaint.

### 6.8 Stdlib integration

- **FR-046** The Rexy stdlib is a *regular Rexy package* named `std`, not a built-in. It lives in its own repository (e.g., `github.com/rexy-lang/std`), publishes to the registry on its own semver track, and is fetched and built by `rxy` like any other dependency. (Removes roadmap Phase 7's "stdlib embedded by CMake" limit.)
- **FR-047** Stdlib versioning is **independent of `rexc` versioning** (decision §9.3). A project pins its stdlib in `Rexy.toml`:
  ```toml
  [dependencies]
  std = "1.4"
  ```
  Projects without an explicit pin get a *default stdlib version per `rexc` release*, declared in the `rexc` distribution and recorded automatically in `Rexy.lock` on first build. This default is overridable by an explicit `std = "x.y"` entry. Implications: the stdlib has its own release cadence, can ship bugfixes without a compiler release, and can deprecate APIs across edition boundaries cleanly.
- **FR-048** The compatibility matrix between `rexc` versions and `std` versions is published as a machine-readable file (`compat.toml`) in the `rexc` distribution. `rxy build` consults this matrix and errors clearly if the project's `std` pin is incompatible with the installed `rexc`.

### 6.9 Native / system dependencies

- **FR-049** A package's `[build] links = ["ssl"]` declares a *requirement on a host-provided library*. `rxy` does not fetch or build native libraries in v1.
- **FR-050** v1 build scripts may invoke `pkg-config` (when installed) via a stdlib helper: `build.probe_pkgconfig("openssl")` returns include dirs, link dirs, and link flags or fails clearly. This is the documented workaround for the "OpenSSL/Homebrew/distro variance" pain.
- **FR-051** A package can declare *minimum* native versions: `[build.system.openssl] min-version = "3.0"`. The build script enforces this via `pkg-config` and emits a clean error: "this project requires libssl ≥ 3.0; pkg-config reports 1.1.1. Install via `brew install openssl@3` (macOS) or `apt install libssl-dev` (Ubuntu)."
- **FR-052** Out of scope (P2): bundling, building, or fetching native libraries. `rxy` will not subsume `vcpkg`/`conan`/Homebrew in v1.

---

## 7. Implementation Phases

Dependency-ordered. Each phase has an exit criterion and is shippable in isolation.

### Phase A — Manifest, lockfile, single-package build
- FR-001 to FR-005, FR-009 to FR-013, FR-026 to FR-029, FR-037 (migrate), FR-041, FR-043.
- **Exit criterion:** `examples/modules/` builds under `rxy build`, no `--package-path`, no `Makefile`. `Rexy.lock` is committed and `--locked` works. `rxy migrate` produces a usable `Rexy.toml` for `examples/modules/`.
- **What's missing here:** no deps, no registry, no build scripts, no workspaces, no cross-compile.

### Phase B — Local & git dependencies + lockfile checksums + vendor
- FR-002 (git deps), FR-010, FR-013, FR-015, FR-019, FR-020, FR-038 (vendor).
- **Exit criterion:** A 2-package toy ecosystem (`util.rx` library + `app.rx` binary) compiles, with `app` depending on `util` via a git URL. Force-pushing a tag in `util` is detected and rejected. `rxy vendor` produces a self-contained tree.

### Phase C — Registry, semver resolution, publish/install/yank
- FR-014, FR-016, FR-018, FR-031 to FR-036.
- **Pre-requisite:** Written registry governance document (R-12) merged before this phase begins.
- **Exit criterion:** `rxy publish util 0.1.0` lands an entry in the registry index repo (with namespace+OWNERS). A fresh consumer can `rxy add util@0.1` and build. `rxy install <bin>` works. `rxy yank` works.

### Phase D — Build scripts + native dep probing
- FR-003 (`[build]`), FR-021 to FR-025, FR-045 (actionable diagnostics for blocked transitive scripts), FR-049–FR-052.
- **Exit criterion:** A package with a `build.rx` that probes `pkg-config` for OpenSSL ≥ 3.0 and generates a `.rx` source file from a JSON schema compiles end-to-end on macOS and Linux. A consumer who has not allow-listed the build script gets a *single, actionable* diagnostic with the audit command.

### Phase E — Workspaces, cross-compile, test, profiles
- FR-006, FR-030, G1-1, G1-2, G1-3.
- **Exit criterion:** `rexc` itself is restructured as a `rxy` workspace. `rxy build --target=x86_64-linux` from macOS produces a working ELF for every workspace member. `rxy test` runs the existing test suite.

### Phase F — Stdlib as a built-in package + `--offline`
- FR-012, FR-046, FR-047, G1-4, G1-5.
- **Exit criterion:** Roadmap Phase 7's known limit ("stdlib embedded by CMake") is gone. Air-gapped offline build works against a primed cache.

### Phase G — Polish
- FR-017 (multi-registry), FR-039 (reserved subcommand stubs), `--json` output everywhere, plugin discovery design doc (P2 implementation).

---

## 8. Risks & Mitigations

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| **R-1** | **Cargo-style feature creep.** Package authors will ask for unification, conditional features, optional deps. Each looks reasonable; together they become Cargo's biggest design regret. | High | High | NG-3 stands. Capabilities are additive only, no negative features, no global unification. Re-evaluate at Phase G with concrete user data. |
| **R-2** | **Git-as-registry brittleness.** Tags get force-pushed. Repos get deleted. Authors disappear. | High | High | FR-013 / FR-018 enforce checksum immutability. The registry-index repo is itself versioned. P2: a checksum-mirror service (Go-modules-style `sumdb`). |
| **R-3** | **`build.rx` as a supply-chain attack vector.** Compiled `.rx` build scripts run with full process privileges. | Medium | High | FR-023: build scripts only run for the *current* package, never transitively unless explicitly opted in. NG-4 blocks install-time execution. `--no-build-scripts` exists. P2: sandboxing. |
| **R-4** | **Build script dependency cycle.** `build.rx` is `.rx`, so it must be compiled by `rexc`. If a package's `build.rx` depends on another package whose own `build.rx` depends on …, the cycle is fatal. | Medium | Medium | v1: build-script dependencies form a separate dependency graph that is resolved before main compilation. Cycles are detected and reported. Build-script deps cannot themselves declare build scripts in v1 (simple two-tier). |
| **R-5** | **Mismatch between `rxy` and `rexc` versions.** A project's `edition = "2026"` may require a `rexc` version the user doesn't have. | Medium | Medium | `rxy --version` reports the embedded/expected `rexc` version. `rxy build` checks `rexc --version` against the project's `edition` and errors clearly. |
| **R-6** | **Lockfile + monorepo merge conflicts.** Every dep change yields a `Rexy.lock` diff; large workspaces collide. | Medium | Medium | v1: structured lockfile (sorted by name+version, one entry per line block). Document `Rexy.lock` as merge-friendly. P2: a `rxy lock --resolve-conflicts` command. |
| **R-7** | **Adoption friction for existing CMake-based `rexc`.** Restructuring `rexc` itself to a `rxy` workspace is non-trivial — ANTLR codegen, downloaded jars, and the CMake build all have to coexist. | Medium | Medium | Phase E does the migration explicitly, with a fallback: the existing CMake build continues to work for `rexc`'s ANTLR step; `rxy` invokes it as an opaque pre-build step (declared via `[build] script = "build.rx"` that shells out). De-risk before committing. |
| **R-8** | **First-user complaint: "Why two tools, `rexc` and `rxy`?"** Go and Zig get away with one. | Low | Low | Mirror Cargo: `rexc` is the compiler, `rxy` is the project tool. Document the boundary. The unified-tool option (`rexc build`) is reversible later as a thin subcommand alias. |
| **R-9** | **The registry index repo becomes a hot spot.** Every publish is a git push; concurrent publishes race. | Low | Medium | Use GitHub's "merge queue" or a small server-side hook that serializes pushes. P2 if it ever becomes a real bottleneck. |
| **R-10** | **Adversarial review surfaced**: build scripts cannot probe for system libraries on Windows hosts because Windows is NG-9. | Low | Low | NG-9 stands. Phase D's exit criterion says "macOS and Linux." Windows host is post-v1, mirroring the language PRD. |
| **R-11** | **"Replace Makefiles" is too broad** — Makefiles also run docs builds, packaging, lints. | Medium | Medium | Scope clarification: `rxy` replaces Makefiles *for Rexy compilation*. The repo's existing `make docs` (pandoc/typst pipeline) is out of scope and will continue to live in the existing `Makefile`. v1 documents this boundary clearly. |
| **R-12** | **Ecosystem trust & ownership.** Adversarial review's biggest call-out: a git-index registry without namespace ownership, transfer rules, malware/abuse handling, reserved names, or provenance becomes a *social* security problem regardless of how sound the lockfile format is. | High | High | FR-035 introduces `[package] namespace` first-come-first-served, recorded in the registry-index repo's `OWNERS` file. FR-036 adds yanking. v1 ships a written **registry governance document** (separate doc) covering: name squatting, transfer requests, malware reports, take-down workflow, reserved namespaces (`rexy-*`, `std-*`). Without this document, *do not* open the registry to public publishing. |
| **R-13** | **Source availability ≠ checksum verification.** Adversarial review: "rebuilds in 2030" requires the source still exists in 2030. Deleted repos, private repos going public-then-private, GitHub outages, LFS-stored blobs, and submodule chains break rebuilds even when checksums are pinned. | High | High | FR-038 (`rxy vendor`) is the v1 answer for projects that need archival reproducibility — vendor before you ship. P2: a checksum-mirror service (Go's `sumdb` model). v1 documents the limitation: "without `rxy vendor`, source availability is git's responsibility, not `rxy`'s." |
| **R-14** | **"Simple resolution" complexity creep.** Adversarial review: the combination of workspaces + platform deps + dev-deps + build-script deps + multiple registries + semver eventually demands SAT-shape behavior, even if v1 says "no SAT." | Medium | High | NG-2 was tightened: v1 *constrains the input space* (no optional deps, no platform-overrides that introduce new packages, no feature flags, no multi-major coexistence). If a future feature would force backtracking, the PRD must be reopened — that is the gate for adopting a real solver. |
| **R-15** | **Ecosystem convention freezing too early.** Adversarial review: package managers cement ecosystem APIs. If Rexy's module layout, stdlib path, ABI, or target model still moves, `rxy` v1 may bake in poor APIs that downstream cannot escape. | Medium | High | Phases A–B ship with **only** local & git deps (no public registry). The public-registry milestone (Phase C) is gated on language-roadmap Phase 8 (Module Policy) being complete. The registry index format is versioned (`registry-format = 1`), enabling format migration. |
| **R-16** | **Native dep variance (OpenSSL/pkg-config/Homebrew/distro)** | High | Medium | FR-049–052 punt the discovery problem to `pkg-config` via a typed build-script API. v1 explicitly does not subsume vcpkg/conan/Homebrew. Diagnostics include OS-specific install hints. |

---

## 9. Resolved Decisions

The five questions originally flagged here have been resolved:

1. **CLI name → `rxy`.** Separate tool from `rexc` (Cargo-style split). The language is "Rexy"; the compiler is `rexc`; the package/build tool is `rxy`. Naming is final for v1.
2. **Edition string → adopted, Rust-style.** Every `Rexy.toml` declares `edition = "<year>"` (e.g., `edition = "2026"`). Editions group breaking-change opt-ins for the language; `rxy build` checks the project's edition against the installed `rexc` and errors with a clear remediation if incompatible. The first edition is `"2026"`. The language PRD will be amended to formalize the edition mechanism (cross-doc dependency captured under R-5).
3. **Stdlib versioning → independent (not pinned 1:1 to `rexc`).** A project pins a specific stdlib version in `Rexy.toml` (`std = "1.4"`); `rxy` resolves it like any other dependency. Implication: stdlib has its own semver track and its own release cadence, the stdlib repo is a workspace member of the `rexy-lang` org, and `rxy` ships a default stdlib version per supported `rexc` so projects without an explicit pin still work. FR-047 is restated below.
4. **Workspace inheritance → Cargo-style (both `[workspace.dependencies]` and `[workspace.package]`/`[workspace.profile]`).** Members inherit dependencies via `foo = { workspace = true }`, package metadata via `[package] license.workspace = true`, and profiles by default with member-level override. Mirrors Cargo's full inheritance surface (with the same documented footguns). FR-006 is restated below.
5. **`Rexy.lock` format → TOML.** Same parser as `Rexy.toml`. Sorted entries for merge-friendliness (already specified in FR-010).

Carryover open questions (none blocking Phase A):

- **A.** Should `rxy` honor a `Rexy.toml`-declared `rust-toolchain.toml`-equivalent (e.g., `[toolchain] rexc = "1.7"`) so a repo pins its compiler version? Defer to Phase E.
- **B.** Does `rxy publish` require a 2FA-style author token, or is the registry-index repo's git push permission sufficient? Decide alongside the registry-governance doc (R-12).

---

## 10. Appendix A — Example `Rexy.toml`

### A.1 Single-package project

```toml
[package]
name = "http"
version = "0.3.1"
edition = "2026"
description = "Minimal HTTP/1.1 client and server primitives"
license = "MIT"
repository = "https://github.com/acme/rexy-http"

[dependencies]
std = "1.4"                                                          # stdlib pinned, FR-047
net = "1.2"
json = { git = "https://github.com/acme/json.rx", version = "0.4" }
tls = { git = "https://github.com/acme/tls.rx", rev = "abc123def" }

[dev-dependencies]
testkit = "0.2"

[build]
script = "build.rx"
links = ["ssl"]

[targets.lib]
path = "src/lib.rx"

[[targets.bin]]
name = "httpc"
path = "src/bin/httpc.rx"

[[targets.test]]
name = "integration"
path = "tests/integration.rx"

[platform.x86_64-apple-darwin.dependencies]
darwin-sys = "0.1"

[profile.dev]
opt-level = 0
debug = true

[profile.release]
opt-level = 3
debug = false
lto = true
strip = true
```

### A.2 Workspace root + members (Cargo-style inheritance, FR-006)

`Rexy.toml` (workspace root):

```toml
[workspace]
members = ["crates/*"]
resolver = "1"

[workspace.package]
version  = "0.4.0"
edition  = "2026"
license  = "MIT"
repository = "https://github.com/acme/rexy-http"

[workspace.dependencies]
std       = "1.4"
net       = "1.2"
json      = { git = "https://github.com/acme/json.rx", version = "0.4" }

[workspace.profile.release]
opt-level = 3
lto       = true
strip     = true
```

`crates/http-core/Rexy.toml` (member):

```toml
[package]
name             = "http-core"
version.workspace = true                # inherits "0.4.0"
edition.workspace = true                # inherits "2026"
license.workspace = true

[dependencies]
std  = { workspace = true }              # inherits version + source
net  = { workspace = true }
json = { workspace = true }
```

`crates/httpd/Rexy.toml` (member that overrides a profile):

```toml
[package]
name             = "httpd"
version.workspace = true
edition.workspace = true

[dependencies]
http-core = { path = "../http-core" }
std       = { workspace = true }

[profile.release]                        # member-level override of workspace profile
strip     = false                        # keep symbols for crash reports
```

## 11. Appendix B — Example `Rexy.lock` entry

```toml
[[package]]
name = "json"
version = "0.4.2"
source = "git+https://github.com/acme/json.rx"
commit = "1f2a3b4c5d6e7f8091a2b3c4d5e6f7a8b9c0d1e2"
checksum = "sha256:9f8e7d6c5b4a3928171615141312111009080706050403020100ffeeddccbbaa"
dependencies = [
  "net 1.2.7",
  "core 0.9.0",
]
```

## 12. Appendix C — Replacing the existing top-level `Makefile`

The repo's current `Makefile` (590 bytes) is essentially `make docs` glue. v1's `rxy` does *not* replace docs builds (R-11). What it *does* replace:

- The hand-rolled `--package-path` flags in `examples/`.
- The CTest invocations in `tests/` (replaced by `rxy test` against a workspace).
- The future "build the Rexy stdlib via CMake-embedding" workaround (Phase F removes it).

The repo's `CMakeLists.txt` continues to build `rexc` itself (the C++17 compiler binary, ANTLR, etc.) — that is a *bootstrap* problem and is out of scope. Once `rexc` is self-hosted (language PRD Phase 7), the bootstrap CMake retires and `rxy build` builds `rexc` itself.

---

## 12.5 Appendix D — Post-Implementation Security Audit & Deferred Work

After the A→G ladder shipped (commits `a8c8ff1` through `c97aa50`), a security audit was conducted via the `octo:personas:security-auditor` agent over the full diff. 15 code-level findings were surfaced. This appendix captures the disposition of each so future sessions know what's done, what's tracked, and what's been intentionally skipped.

Full audit transcript: `~/.claude-octopus/results/` (delivery reports + `tangle-validation-rxy-*.md`).

### Status legend

- ✅ **Fixed** in commits `9936f8f` (first hardening pass), `eab04c7` (deferred-HIGH pass), or the deferred-MEDIUM clearance pass that follows this appendix
- 🟡 **Deferred** — tracked, with estimated cost
- ⊘ **Skipped** — false alarm or out of scope, with reason

### Findings

| # | Severity | Finding | Status | File:line | Notes |
|---|---|---|---|---|---|
| 1 | HIGH | Path traversal via TOML quoted dep keys | ✅ Fixed | `manifest.cpp` (parse_deps_table) | `is_valid_package_name` + `assert_safe_name` defense in depth |
| 2 | HIGH | Build-script OUT_DIR bypass via symlinks | ✅ Fixed | `build_script.cpp`, `hash/sha256.cpp` | Reject symlinks in source-tree walks; fingerprint = (mtime, size, sha256) |
| 3 | HIGH | mtime spoofing via utime() | ✅ Fixed | `build_script.cpp` | Same fingerprint mechanism — sha256 is the load-bearing element |
| 4 | HIGH | tar extraction allows symlink/abs-path escape | ✅ Fixed | `cli.cpp` (cmd_publish) | `--no-same-owner --no-same-permissions`; macOS bsdtar already rejects `..` |
| 5 | HIGH | Registry TOML re-serialization injection | ✅ Fixed | `registry.cpp` | `toml_escape()` + unified `serialize_package()` |
| 6 | MEDIUM | Signal handler races on global atomic | ✅ Fixed | `process.cpp` | Lock-free 32-slot active-children table + once-installed forwarder; async-signal-safe via `std::atomic<pid_t>::is_always_lock_free` |
| 7 | MEDIUM | `chdir` race in `process::run` | ✅ Fixed | `process.cpp` | `posix_spawn_file_actions_addchdir_np` when available (macOS 10.15+, glibc 2.29+); mutex-serialized parent-`chdir` fallback otherwise |
| 8 | MEDIUM | Force-pushed git tag silently rewrites cache | ✅ Fixed | `source.cpp` (resolve_git) | `ResolveOptions::locked_commits` populated from prior `Rexy.lock` |
| 9 | MEDIUM | `cmd_publish` uses `rand()` for tempdir | ✅ Fixed | `cli.cpp` | `mkdtemp(3)` |
| 10 | MEDIUM | Concurrent rxy instances race on cache | ✅ Fixed | `source.cpp`, `cache.cpp` | `cache::FileLock` (flock LOCK_EX, RAII) per bare-URL and per `(name,commit)` worktree; cross-process verified by `tests/rxy/cache_tests.cpp` |
| 11 | MEDIUM | `/usr/bin/env git` PATH-ordering trap | ✅ Fixed | `registry.cpp`, `cli.cpp` | `util::find_on_path("git", "GIT")` everywhere |
| 12 | MEDIUM | ANSI escape injection in diagnostics | ✅ Fixed | `diag.cpp` | `strip_ansi()` applied to every user-controlled string before `fprintf` |
| 13 | LOW | tmp.tar inside hashed workdir | ⊘ Skipped | `source.cpp` | False alarm — `.tmp.tar` is removed before `sha256_dir_tree` runs |
| 14 | LOW | SHA-256 bitlen overflow at 2^61 bytes | ⊘ Skipped | `hash/sha256.cpp` | Unreachable; filesystem upper bound is way below 2^61 |
| 15 | LOW | `cmd_remove` deleted lines from any TOML section | ✅ Fixed | `cli.cpp` | Section-aware scanner; only edits `[dependencies]` / `[dev-dependencies]` |

**Tally:** 12 fixed, 0 deferred, 2 skipped (one false alarm + one unreachable), 1 out-of-scope (audit #10's "submitter bot" lives in registry-governance ops, not in rxy code).

### Deferred work — cleared

The original three deferred items (#6/#7 signal+chdir, #10 cache flock, workspace-root build dispatch) all shipped in the post-Appendix-D clearance pass. Concrete shape of each:

| Item | Resolution | Test coverage |
|---|---|---|
| **#6/#7 Signal & chdir refactor** | `process.cpp` now keeps a lock-free 32-slot table of active child PIDs (`std::atomic<pid_t>`, lock-free static-asserted) and forwards SIGINT/SIGTERM to all live children. Signal handlers are installed exactly once; when no children are live, the handler restores `SIG_DFL` and re-raises so the parent exits as the user expects. `cwd` change runs in the child via `posix_spawn_file_actions_addchdir_np` when CMake detects it; on older systems a process-wide mutex serializes the parent-`chdir`/spawn/restore window. | `tests/rxy/process_tests.cpp` (cwd round-trip; 64 sequential spawns; failed-spawn slot release) |
| **#10 `flock` on `~/.rxy` cache** | `cache::FileLock` RAII wrapper around `flock(2) LOCK_EX` (with EINTR loop). `cache::lock_bare_for_url(url)` and `cache::lock_src(name, commit)` produce per-bucket lockfiles (`<bare-sha256>.lock`, `<commit>.lock`). `source::resolve_git` holds the bare lock across clone/fetch/rev-parse and the src lock across the `.rxy_extracted` check + tar extraction so concurrent rxy processes resolving the same URL/commit serialize correctly. | `tests/rxy/cache_tests.cpp` (release-on-destruction; cross-process fork blocking test that proves the lock is observed across PIDs) |
| **Workspace-root build dispatch** | `cmd_build` detects a workspace-only manifest (`m.package.name.empty()` + `[workspace]` table), enumerates members via `workspace::load`, and walks them in sorted order. Default behavior is fail-fast; `--keep-going` builds every member and reports `N ok, M failed` while still exiting non-zero on any failure. `--bin` is rejected at a workspace root (no defensible cross-member meaning). | `tests/rxy/smoke_h.sh` (fail-fast, `--keep-going`, `--bin` rejection, all-green path) |

### Audit failure mode worth recording

The dispatched `octo:droids:octo-code-reviewer` agent failed identically across two phases (Phase A delivery and post-A→G audit): it produces markdown code blocks with embedded shell commands instead of invoking its tools, then returns `STATUS: BLOCKED, Confidence: 0`. Reproduced twice in this work tree. The `octo:personas:security-auditor` agent works correctly. Avoid `octo-code-reviewer` until that's fixed; route review work through `security-auditor` or do the review via direct file reads.

### Lessons captured

1. **Don't trust user-controlled strings inside any path computation.** Several findings (#1, #5, #11) all stem from "value flows from manifest/env/git into a filesystem operation without validation". A general policy of "validate at the input boundary, assert at the use boundary" closes most of this surface area.
2. **mtime is not a tamper-evidence mechanism.** Finding #3 was nearly a tautology in retrospect — `utime(2)` is in the stdlib of every system rxy targets. Content-addressed hashing is the only meaningful tamper detection.
3. **`/usr/bin/env <cmd>` is convenient but trades safety for short-term convenience.** Finding #11 surfaced because we used it to avoid hardcoding `/usr/bin/git`. The right pattern is a `find_on_path()` resolver that honors a per-binary env override (`$GIT`, `$REXC`, etc.) and validates the resolution.
4. **Multi-LLM dispatch produces real value when the agent works.** The `security-auditor` agent's 15-finding report drove three commits' worth of fixes that I would not have surfaced alone. The `code-reviewer` agent failing meanwhile is a useful negative signal — not every advertised tool is reliable, and the workflow needs to gracefully fall back.

---

## 13. Self-Score (100-point AI-Optimization Framework)

| Category | Max | Score | Rationale |
|---|---|---|---|
| **AI-Specific Optimization** | 25 | 23 | FRs are numbered (52), traceable to goals, P0/P1 levels explicit. Phases are dependency-ordered with hard exit criteria. Non-goals enumerated and rationale'd. Adversarial-review feedback incorporated as new FRs (37/38/45/49–52) and new Risks (R-12 to R-16). Could improve: a machine-readable `goals → FRs → phases` matrix as YAML frontmatter. |
| **Traditional PRD Core** | 25 | 23 | Problem statement is quantified per pain point; personas are 3 specific user types; success metrics include baseline + target. Risks now have explicit ecosystem-trust and source-availability coverage. |
| **Implementation Clarity** | 30 | 28 | 7 implementation phases (A–G), each shippable, each with exit criteria. Cache layout, command surface, manifest schema, and lockfile schema are concretely specified with examples. Phase C now gated on the registry-governance doc. Could improve: explicit per-FR effort estimates and dependency arrows between FRs. |
| **Completeness** | 20 | 18 | Includes companion-doc cross-references, appendices with worked examples, replaces-Makefile boundary clarification, open questions section, migration tooling (`rxy migrate`), archival reproducibility (`rxy vendor`), native-dep probing API. |
| **Total** | **100** | **92** | |

**Adversarial review:** applied (provider: Codex). Five challenges incorporated:
1. Source availability (≠ checksum) → FR-038 `rxy vendor`, R-13.
2. "Simple resolution" creep → tightened NG-2, R-14.
3. Yanking/deprecation gap → FR-036 `rxy yank`.
4. Native dep variance → FR-048–FR-051, R-16.
5. Ecosystem trust/ownership → FR-035 namespacing, registry-governance doc gate, R-12.

Two challenges acknowledged but not adopted:
- "Build scripts run during `rxy install`" — accepted as intentional; documented in FR-034.
- "Timing risk: cementing conventions early" — captured in R-15; mitigated by gating Phase C on language-roadmap Phase 8 and a versioned registry format.

---

*Companion docs: `docs/prd.md` (language PRD, NG-4 supersedure), `docs/roadmap.md` (Phase 8 elevation, Phase 8+ stretch elevation).*
