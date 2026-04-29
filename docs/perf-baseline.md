# Rexy Phase 2 Perf Baseline

**Captured:** 2026-04-29 (FE-109d landing)
**Host:** Apple Silicon (arm64) macOS
**Build:** `cmake --build build` (Release; CMakeLists default optimization)

This document records compile-time and IR-shape baselines for the
Phase 2 collections fixtures. The numbers anchor the
`collections_hashmap_demo_compile_perf_snapshot` test (in
`tests/ir_tests.cpp`) and serve as a regression reference.

Runtime micro-benchmarks are intentionally NOT in this document.
`src/stdlib/std/time.rx` only exposes second-resolution
(`sys_unix_seconds`); without a `sys_unix_nanos`-equivalent primitive,
the demos run faster than the timing floor — process startup alone is
~2 ms on this host, which dominates any workload these fixtures
construct. Once a sub-millisecond timer lands, this document grows a
runtime section that measures hot loops at scale.

## Compile-time (rexc → arm64-macos `.s`, fastest of 20 runs)

| Fixture | Compile time |
|---|---:|
| `examples/uninit_let_demo.rx` | ~30 ms (smallest) |
| `examples/defer_demo.rx` | 38.3 ms |
| `examples/arena_vec_demo.rx` | 38.3 ms |
| `examples/generic_vec_demo.rx` | 38.8 ms |
| `examples/hash_demo.rx` | 38.8 ms |
| `examples/hashmap_demo.rx` | 39.8 ms |

Compile time is dominated by ANTLR parser construction and stdlib
ingestion (every compile re-parses the embedded stdlib). Variation
across fixtures is small (~1.5 ms) because user-code parsing /
sema / lower / codegen is a small fraction of total time. Once an
incremental compile mode lands, this column splits into "cold"
vs "warm".

## Code size (arm64-macos)

| Fixture | `.s` lines | Stripped binary bytes |
|---|---:|---:|
| `examples/uninit_let_demo.rx` | 52 | 74,992 |
| `examples/defer_demo.rx` | 116 | 75,064 |
| `examples/arena_vec_demo.rx` | 374 | 75,136 |
| `examples/hash_demo.rx` | 410 | 75,048 |
| `examples/generic_vec_demo.rx` | 807 | 75,360 |
| `examples/hashmap_demo.rx` | 1,110 | 75,392 |

Binary size is dominated by the macOS Mach-O headers + page alignment
floor (~74 KiB minimum) — all fixtures land in the same 4 KiB page.
The `.s` line count is the meaningful code-size signal: it scales
with monomorph count and per-op statement count, and is what the
`hashmap_demo_compile_perf_snapshot` test pins via `count_ir_statements`.

## IR shape (hashmap_demo's main, FE-109d snapshot)

The compile-time perf assertion in `tests/ir_tests.cpp` tracks two
ranges. Tightening these requires a deliberate decision — see the
test comment for the loosen-vs-investigate guidance.

| Metric | Asserted range | Actual at landing |
|---|---:|---:|
| Total functions in module (incl. stdlib pulled by `StdlibSymbolPolicy::All`) | 3 – 200 | within range |
| `main` IR statements | 3 – 40 | within range |

## Runtime (process startup floor)

| Fixture | Fastest of 50 cold runs |
|---|---:|
| `examples/uninit_let_demo.rx` | ~2.0 ms |
| `examples/generic_vec_demo.rx` | ~2.0 ms |
| `examples/hash_demo.rx` | ~2.0 ms |
| `examples/hashmap_demo.rx` | ~2.1 ms |
| `examples/arena_vec_demo.rx` | ~2.1 ms |
| `examples/defer_demo.rx` | ~2.1 ms |

All fixtures cluster at the macOS process-startup floor. None of these
numbers reflect collection performance — they reflect `execve` + dynamic
loader + `_main` prologue. A useful runtime baseline waits on
sub-millisecond timing primitives.

## How to refresh

```sh
# Compile-time
for demo in examples/*.rx; do
    name=$(basename $demo .rx)
    python3 -c "
import subprocess, time
runs = []
for _ in range(20):
    t0 = time.perf_counter_ns()
    subprocess.run(['./build/rexc', '$demo', '--target', 'arm64-macos', '-S', '-o', '/tmp/probe.s'], capture_output=True)
    t1 = time.perf_counter_ns()
    runs.append(t1 - t0)
print(f'$name: {min(runs)//1000} us')
"
done

# Code size
for demo in examples/*.rx; do
    name=$(basename $demo .rx)
    ./build/rexc $demo --target arm64-macos -S -o /tmp/$name.s
    ./build/rexc $demo --target arm64-macos -o /tmp/$name
    echo "$name: $(wc -l < /tmp/$name.s) lines, $(stat -f%z /tmp/$name) bytes"
done
```
