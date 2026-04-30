# `rxy` Plugin Discovery — Design Sketch

**Status:** P2 design doc (not implemented in v1)
**Companion:** `docs/prd-package-manager.md` Phase G

This document sketches the future PATH-based subcommand resolver — the same mechanism `git`, `cargo`, and `kubectl` use to support third-party subcommands without modifying the core binary.

## The Cargo precedent

When you run `cargo foo`, Cargo:
1. Tries built-in subcommands first.
2. If `foo` is not built-in, walks `$PATH` for `cargo-foo`.
3. If found, exec's `cargo-foo` with the remaining argv.
4. Each plugin is just a binary anywhere on PATH that matches `cargo-<name>`.

This means **no plugin registry, no manifest, no registration step.** Drop a binary on PATH, and it's a subcommand.

## The proposed rxy model

Same shape:

```
rxy outdated         # tries built-in first; not found → exec rxy-outdated
rxy publish-helper   # exec's rxy-publish-helper if on PATH
```

### Resolution order

1. **Built-in subcommands** (`new`, `init`, `build`, `run`, `test`, `add`, `remove`, `publish`, `yank`, `unyank`, `lockfile`, `fmt`, `doc`, `bench`) — fastest path, no PATH walk.
2. **Reserved-but-not-yet-implemented** (`fmt`, `doc`, `bench`) — the in-tree stub wins; users CAN'T shadow these names with plugins.
3. **`rxy-<name>` on PATH** — exec into the plugin.
4. **Not found** — error: `unknown command \`foo\`; not found as a built-in or as `rxy-foo` on PATH`.

### Argv passthrough

```
rxy foo --bar baz  →  exec rxy-foo --bar baz
```

All flags after the subcommand pass to the plugin verbatim. Global flags (`--quiet`, `--verbose`, `--color`, `--manifest-path`, `--offline`) are exported as env vars before exec so plugins can honor them without re-parsing:

```
RXY_QUIET=1
RXY_VERBOSE=0
RXY_COLOR=auto
RXY_MANIFEST_PATH=/path/to/Rexy.toml
RXY_OFFLINE=0
RXY_HOME=~/.rxy
```

### Binary naming

- Lowercase, hyphen-separated, must start with letter.
- `rxy-<name>` only — no nested namespaces in v1.
- `rxy-help-<topic>` is a future convention for `rxy help <topic>` style routing (defer).

### Discoverability — `rxy plugins list`

Built-in subcommand that walks PATH, lists every `rxy-*` binary it finds:

```
$ rxy plugins list
INSTALLED PLUGINS:
  rxy-outdated   /Users/drew/.rxy/bin/rxy-outdated
  rxy-vendor     /Users/drew/.cargo-style/bin/rxy-vendor
  rxy-tree       /usr/local/bin/rxy-tree
```

### Distribution

- Plugins are regular Rexy packages with a `[[targets.bin]]` whose name starts with `rxy-`.
- `rxy install rxy-outdated@^0.4` (FR-034 once it lands) puts the binary on PATH at `$REXY_HOME/bin/`.
- `~/.rxy/bin` should already be on PATH (documented in install instructions).

## What's NOT in scope for v1

- A plugin manifest format (Cargo doesn't have one; we don't either).
- Plugin signing / provenance (relies on registry trust per Phase C governance).
- A "plugin marketplace" page (registry browsing is Phase C+ UX).
- Sandbox / capability scoping (every plugin is a regular binary with full filesystem access — same as `cargo`).

## Implementation effort

Roughly 50 LOC in `cli.cpp::dispatch()`:

```cpp
// After built-in dispatch, fall back to PATH lookup.
auto plugin = util::search_path("rxy-" + p.subcommand);
if (plugin) {
    process::Options popts;
    popts.cwd = fs::current_path();
    popts.stream_through = true;
    popts.env_overlay["RXY_QUIET"]   = p.g.quiet ? "1" : "0";
    popts.env_overlay["RXY_VERBOSE"] = p.g.verbose ? "1" : "0";
    // ...
    process::Result pr = process::run(*plugin, p.sub_args, popts);
    return pr.exit_code;
}
```

The `util::search_path()` helper already exists (it's how `find_rexc()` walks PATH). Wiring this in is straightforward; it's deferred as P2 because:

- Nobody has plugins yet (the ecosystem starts at 0).
- `rxy install` (FR-034 binary copy) needs to land first to make distribution easy.
- Phase G's deliverable is the design contract; the implementation comes when there's a real first plugin.

## Future-proofing

By NOT shipping the resolver in v1 we keep the door open for richer designs:

- Could decide on a plugin manifest format later (Cargo regrets not having one for declaring "plugin needs cargo ≥ X.Y").
- Could add capability declarations (`needs-network = true`).
- Could add a plugin index in the registry-governance doc — pre-vet trusted plugins, similar to GitHub Actions Marketplace.

But those are forks in the road. v1 ships the design intent so users who want plugins know what's coming.
